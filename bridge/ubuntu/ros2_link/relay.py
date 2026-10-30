"""``ros2_link.relay`` - the connector between two gRPC agents.

The simulator (``ros2_bridge_sim``) and a genuine ROS2 graph
(``ros2_grpc_sdk_agent --backend rclcpp``) are two *separate* gRPC servers.
They speak the same protocol but they do not share a graph: the simulator's
topics live in a Python ``graph.Graph`` and never reach DDS, and the real
ROS2 graph is invisible to the simulator.

This module is the missing piece. It is a gRPC **client** to both of them and
a gRPC **server** to everybody downstream:

    real ROS2 agent  <--Subscribe--  relay  --Subscribe-->  sim agent
                          ^                    |
                          |                    v
                          +----- Publish ----+-->  Windows daemon -> shm -> nav2

Two properties make this work and both are why no re-encoding happens:

* the payload on the wire is CDR, with the 4 byte encapsulation header, in
  both directions (see ``ros2_bridge.proto``, the ``Message.data`` comment);
* therefore the relay can forward bytes **verbatim**. It never has to know a
  message schema, so a type the simulator does not implement still crosses.

Three things the relay has to get right, and this module is where they live:

* **no echo** - a message the relay injects into an upstream comes straight
  back to the relay's own subscription; without the mute it would be merged
  again and handed to the downstream a second time;
* **no mixing** - the simulator and a real robot both publish ``/scan`` and
  ``/odom``, and merging them into one topic would interleave two different
  robots' data. Colliding topics are therefore kept apart (see
  :data:`COLLISION_POLICIES`);
* **no lost services and actions** - a merged graph that only carries topics
  is half a graph. Services and actions are advertised too, and a call or a
  goal is routed to the upstream that owns it.
"""

from __future__ import annotations

import argparse
import logging
import signal
import sys
import threading
import time
from concurrent import futures

import grpc

from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc

LOG = logging.getLogger("ros2_link.relay")

#: How long to wait for an agent to answer before calling it dead.
DEFAULT_PROBE_TIMEOUT_S = 2.0

#: How long an action round trip may take. Accepting a goal is not an
#: instant read: the upstream first has to discover its own action server and
#: hand the goal over, which on a real ROS2 graph can take a few seconds.
#: The probe timeout is far too short for that.
DEFAULT_ACTION_TIMEOUT_S = 20.0

#: Server worker threads. Every long-lived ``Subscribe`` (one per mirrored
#: topic) holds a worker for its whole life, and the daemon mirrors dozens of
#: topics at once. The pool therefore has to cover the streams *plus* the
#: unary calls; too small a pool lets the streams starve the calls and the
#: relay stops answering.
SERVER_MAX_WORKERS = 512

#: How often the relay re-reads the topic/service/action lists of each upstream.
DEFAULT_REFRESH_S = 2.0

#: Topics that are never relayed: the log and the parameter stream are noise
#: for the downstream and they change on every node's startup.
DEFAULT_EXCLUDE = ("/rosout", "/rosout_ros", "/parameter_events")

#: What to do when both upstreams publish the same topic name.
#:
#: ``prefer-sim``  the simulator keeps the plain name (``/scan``) and the real
#:                 robot's is published under ``/real/scan``;
#: ``prefer-real`` the other way round (``/sim/scan`` for the simulator).
#:
#: Either way **no data is lost**: the other side is still there, just under
#: a name of its own. Merging them into one topic would interleave two
#: robots' data, and the downstream could not tell them apart.
COLLISION_POLICIES = ("prefer-sim", "prefer-real")


def _split_addr(addr: str) -> tuple[str, int]:
    """``"host:port"`` -> ``("host", port)``."""
    if ":" not in addr:
        raise ValueError("expected host:port, got %r" % addr)
    host, _, port = addr.rpartition(":")
    return host or "127.0.0.1", int(port)


class _Upstream:
    """One agent the relay talks to."""

    def __init__(self, name: str, addr: str) -> None:
        self.name = name
        self.addr = addr
        self.host, self.port = _split_addr(addr)
        self.target = "%s:%d" % (self.host, self.port)
        self.channel: grpc.Channel | None = None
        self.stub: pb_grpc.Ros2BridgeStub | None = None
        #: topic -> (type, publisher_count, subscriber_count) as the upstream
        #: reports it. The counts are kept so the merged graph can report the
        #: same numbers a real (merged) DDS graph would - the Windows daemon
        #: decides what to mirror from them.
        self.topics: dict[str, tuple[str, int, int]] = {}
        #: every topic the upstream knows, including the ones it only
        #: *subscribes* to. `topics` is filtered to sources (somebody
        #: publishes); this one is needed to find the sink of a command.
        self.all_topics: dict[str, tuple[str, int, int]] = {}
        #: service name -> service type
        self.services: dict[str, str] = {}
        #: action name -> action type
        self.actions: dict[str, str] = {}
        #: topic -> the thread currently streaming it
        self.subs: dict[str, threading.Thread] = {}
        self.alive = False

    def connect(self) -> bool:
        """Open the channel and probe the agent."""
        try:
            if self.channel is None:
                self.channel = grpc.insecure_channel(self.target)
                grpc.channel_ready_future(self.channel).result(timeout=DEFAULT_PROBE_TIMEOUT_S)
            self.stub = pb_grpc.Ros2BridgeStub(self.channel)
            resp = self.stub.Ping(pb.PingRequest(payload="ros2_link"), timeout=DEFAULT_PROBE_TIMEOUT_S)
            self.alive = True
            LOG.info(
                "upstream %-5s %s is up (agent_version=%s ros_distro=%s)",
                self.name, self.target, resp.agent_version, resp.ros_distro,
            )
            return True
        except Exception as exc:  # noqa: BLE001 - any failure means "not up"
            LOG.warning("upstream %-5s %s is not reachable: %s", self.name, self.target, exc)
            self.alive = False
            self.stub = None
            return False

    def close(self) -> None:
        for thread in list(self.subs.values()):
            thread.join(timeout=0.5)
        self.subs.clear()
        if self.channel is not None:
            self.channel.close()
            self.channel = None
        self.stub = None
        self.alive = False


class Relay:
    """Merges the graphs of two agents and republishes the result.

    The merged graph lives in a :class:`Graph` from the simulator package, so
    the relay can reuse the very same serving code the simulator uses
    (``ros2_bridge_sim.agent.BridgeAgent``). That keeps the downstream face
    byte-for-byte what the Windows daemon already expects.
    """

    #: How long a topic stays muted after the relay published into it. Long
    #: enough to swallow the upstream's echo of our own message, short enough
    #: not to swallow a legitimate message that follows.
    MUTE_S = 0.5

    def __init__(
        self,
        sim: "_Upstream",
        real: "_Upstream",
        *,
        exclude: tuple[str, ...] = DEFAULT_EXCLUDE,
        refresh_s: float = DEFAULT_REFRESH_S,
        collision: str = "prefer-sim",
    ) -> None:
        self.sim = sim
        self.real = real
        self.exclude = exclude
        self.refresh_s = refresh_s
        if collision not in COLLISION_POLICIES:
            raise ValueError("collision policy must be one of %s" % (COLLISION_POLICIES,))
        self.collision = collision
        self.graph = _make_graph()
        self._stop = threading.Event()
        self._lock = threading.Lock()
        #: merged topic name -> (upstream name, the name on that upstream)
        self._route: dict[str, tuple[str, str]] = {}
        #: merged topic name -> (type, publisher_count, subscriber_count)
        self._counts: dict[str, tuple[str, int, int]] = {}
        #: service name -> (upstream name, service type)
        self._services: dict[str, tuple[str, str]] = {}
        #: action name -> (upstream name, action type)
        self._actions: dict[str, tuple[str, str]] = {}
        #: topic -> monotonic deadline until which an upstream message is
        #: dropped. Set right before the relay publishes *into* an upstream:
        #: that upstream hands the message straight back to the relay's own
        #: subscription, and mirroring it again would echo it downstream.
        self._mute_until: dict[str, float] = {}
        #: how many messages crossed in each direction
        self.counts = {"sim->down": 0, "real->down": 0, "down->sim": 0, "down->real": 0}
        self._servers: list[grpc.Server] = []

    # -- naming -------------------------------------------------------------

    @property
    def _loser(self) -> str:
        """Which upstream loses a collision, and gets a prefix."""
        return "real" if self.collision == "prefer-sim" else "sim"

    def _merged_name(self, upstream_name: str, topic: str) -> str:
        """The name a topic is published under in the merged graph.

        A topic only one upstream publishes keeps its name. A topic both of
        them publish is split: the winner keeps the plain name, the other
        one is published under ``/<upstream>/<topic>`` so the downstream can
        tell the two apart instead of receiving an interleaved mess.
        """
        other = self.real if upstream_name == "sim" else self.sim
        if topic in other.topics and upstream_name == self._loser:
            return "/%s/%s" % (upstream_name, topic.lstrip("/"))
        return topic

    # -- the merged graph ---------------------------------------------------

    def _publish_merged(self, topic: str, type_name: str, data: bytes, stamp_ns: int, origin: str) -> None:
        """Put a message from one upstream into the merged graph."""
        if topic in self.exclude:
            return
        now = time.monotonic()
        with self._lock:
            if self._mute_until.get(topic, 0.0) > now:
                # We injected this topic ourselves a moment ago; the upstream
                # is only handing our own message back. Dropping it here is
                # what keeps a publish from echoing to the downstream.
                return
        try:
            self.graph.publish(topic, type_name, data, stamp_ns=stamp_ns)
        except Exception as exc:  # noqa: BLE001 - one bad message must not kill the relay
            LOG.warning("cannot merge %s from %s: %s", topic, origin, exc)
            return
        self.counts["%s->down" % origin] = self.counts.get("%s->down" % origin, 0) + 1

    def _origin_of(self, topic: str) -> str:
        with self._lock:
            route = self._route.get(topic)
        return route[0] if route else "sim"

    # -- upstream -> merged -------------------------------------------------

    def _stream_topic(self, upstream: _Upstream, topic: str, type_name: str) -> None:
        """Subscribe to one topic on one upstream and merge what comes."""
        assert upstream.stub is not None
        request = pb.SubscribeRequest(topic=topic, type=type_name)
        try:
            for message in upstream.stub.Subscribe(request):
                if self._stop.is_set():
                    return
                self._publish_merged(message.topic or topic, message.type or type_name,
                                     message.data, message.stamp_ns, upstream.name)
        except grpc.RpcError as exc:
            if not self._stop.is_set():
                LOG.debug("subscription on %s/%s ended: %s", upstream.name, topic, exc.code())
        except Exception as exc:  # noqa: BLE001
            if not self._stop.is_set():
                LOG.warning("subscription on %s/%s failed: %s", upstream.name, topic, exc)

    def _pump(self, upstream: _Upstream) -> None:
        """Keep one upstream's subscriptions in sync with its topic list.

        The upstream may not be there yet - a real ROS2 agent that is started
        after the relay is a normal order of things, not a failure. So the
        pump keeps trying instead of asserting and dying: a dead pump thread
        would silently freeze that half of the merged graph forever.
        """
        while not self._stop.is_set():
            if upstream.stub is None:
                # Not connected (yet). Try again; `connect` is cheap and
                # re-establishes the stub when the agent shows up.
                if upstream.connect():
                    LOG.info("upstream %s came up", upstream.name)
                else:
                    time.sleep(self.refresh_s)
                    continue
            try:
                resp = upstream.stub.ListTopics(pb.ListTopicsRequest())
                # Everything the upstream knows, sinks included.
                all_topics = {
                    t.name: (t.type, t.publisher_count, t.subscriber_count)
                    for t in resp.topics if t.name
                }
                # A topic with no publisher is a *sink* on this upstream (the
                # simulator's /cmd_vel only subscribes, for example). Mirroring
                # it would carry messages that originated downstream back to
                # downstream, so only topics somebody actually publishes are
                # treated as sources.
                topics = {
                    name: info
                    for name, info in all_topics.items()
                    if info[1] > 0 or info[1] < 0
                }
            except Exception as exc:  # noqa: BLE001
                if not self._stop.is_set():
                    LOG.debug("cannot list topics on %s: %s", upstream.name, exc)
                # The agent is gone or the channel broke: drop the stub so the
                # next round reconnects.
                upstream.alive = False
                upstream.stub = None
                time.sleep(self.refresh_s)
                continue

            upstream.topics = topics
            upstream.all_topics = all_topics

            # Services and actions are part of the graph too. A merged graph
            # that carried only topics would be half a graph: the downstream
            # could not call the robot's services or send it a goal.
            self._sync_services(upstream)
            self._sync_actions(upstream)

            # Register the topics in the merged graph right away. The graph
            # creates a topic lazily on its first message, so without this the
            # merged graph would look empty until data flowed - and a
            # downstream `ros2 topic list` would answer "no topics" on a graph
            # whose topics are merely idle. `_ensure` is the graph's own
            # "create it if it is not there yet" helper.
            #
            # The names are the *merged* names, so a topic that lost a
            # collision is registered under its prefixed name.
            merged_names = {}
            for name, (type_name, publishers, subscribers) in topics.items():
                merged = self._merged_name(upstream.name, name)
                merged_names[name] = merged
                if merged in self.exclude:
                    continue
                with self._lock:
                    self._route[merged] = (upstream.name, name)
                    self._counts[merged] = (type_name, publishers, subscribers)
                try:
                    self.graph._ensure(merged, type_name)
                except Exception as exc:  # noqa: BLE001
                    LOG.debug("cannot register %s in the merged graph: %s", merged, exc)

            # stop streaming topics the agent no longer has
            for name in list(upstream.subs):
                if name not in topics:
                    thread = upstream.subs.pop(name)
                    # the thread ends on its own when the RPC fails; it is
                    # daemon-threaded, so there is nothing to join hard
                    del thread

            # start streaming topics we do not have yet
            for name, (type_name, _pub, _sub) in topics.items():
                if name in upstream.subs or name in self.exclude:
                    continue
                thread = threading.Thread(
                    target=self._stream_topic,
                    args=(upstream, name, type_name),
                    name="sub-%s-%s" % (upstream.name, name),
                    daemon=True,
                )
                upstream.subs[name] = thread
                thread.start()

            time.sleep(self.refresh_s)

    # -- merged -> upstream -------------------------------------------------

    def _publish_back(self, topic: str, type_name: str, data: bytes) -> None:
        """Send a message that came from downstream back to where it acts.

        "Back to its origin" is the wrong rule for a command. ``/cmd_vel`` is
        published by the *real* ROS2 graph (nav2's velocity smoother) and
        consumed by the *simulator* (the car). A command published from
        downstream means "act on this", so it has to go to the side that
        consumes it - otherwise the Windows controller drives the real robot's
        output topic and the car never moves.

        So the order is:

        1. an upstream that only *subscribes* to the topic is the actor and
           gets the message;
        2. otherwise the topic's own source does (the merged topic's owner);
        3. otherwise the simulator, which is the side that acts on commands.
        """
        with self._lock:
            route = self._route.get(topic)
        upstream_topic = route[1] if route is not None else topic

        actor = None
        for upstream in (self.sim, self.real):
            info = upstream.all_topics.get(upstream_topic)
            if info is not None and info[1] == 0 and info[2] > 0:
                actor = upstream
                break

        if actor is not None:
            target = actor
            owner = actor.name
        elif route is not None:
            owner = route[0]
            target = self.sim if owner == "sim" else self.real
        else:
            # A topic nobody publishes (the simulator's /cmd_vel is one) is
            # still writable from downstream. It goes to the simulator, which
            # is the side that acts on a command.
            owner, target = "sim", self.sim
        if target.stub is None:
            return
        # Mute the topic for a moment before the injection: the upstream
        # delivers our own message straight back to this relay's subscription,
        # and without the mute it would be merged again and echoed downstream.
        with self._lock:
            self._mute_until[topic] = time.monotonic() + self.MUTE_S
        try:
            target.stub.Publish(
                pb.PublishRequest(topic=upstream_topic, type=type_name, data=data, encoding="cdr"),
                timeout=DEFAULT_PROBE_TIMEOUT_S,
            )
            self.counts["down->%s" % owner] = self.counts.get("down->%s" % owner, 0) + 1
        except Exception as exc:  # noqa: BLE001
            LOG.warning("cannot publish %s back to %s: %s", topic, owner, exc)
            with self._lock:
                self._mute_until.pop(topic, None)

    # -- services -----------------------------------------------------------

    def _sync_services(self, upstream: _Upstream) -> None:
        """Read the upstream's service list and route it into the merged graph."""
        try:
            resp = upstream.stub.ListServices(pb.ListServicesRequest())
        except Exception as exc:  # noqa: BLE001
            LOG.debug("cannot list services on %s: %s", upstream.name, exc)
            return
        services = {s.name: s.type for s in resp.services if s.name}
        upstream.services = services
        with self._lock:
            # The simulator's services live under /sim/ and the real ROS2
            # graph's under their own names, so they rarely collide. When
            # they do, the real ROS2 graph wins - it is the one whose
            # implementation is the "real" answer.
            if upstream.name == "sim":
                for name, type_name in services.items():
                    self._services.setdefault(name, (upstream.name, type_name))
            else:
                for name, type_name in services.items():
                    self._services[name] = (upstream.name, type_name)

    def _service_route(self, name: str) -> tuple[_Upstream, str] | None:
        """The upstream that serves ``name``, and the name it knows it by."""
        with self._lock:
            entry = self._services.get(name)
        if entry is None:
            return None
        owner, type_name = entry
        upstream = self.sim if owner == "sim" else self.real
        return upstream, type_name

    def _call_service(self, request, context):  # noqa: ANN001 - grpc callback
        route = self._service_route(request.name)
        if route is None:
            return pb.CallServiceResponse(success=False, error="no service named %r" % request.name)
        upstream, type_name = route
        if upstream.stub is None:
            return pb.CallServiceResponse(success=False,
                                          error="upstream %s is not reachable" % upstream.name)
        # The upstream knows the service by the name it advertised, which is
        # the same one the downstream used - the relay does not rename
        # services, only topics that collide.
        try:
            return upstream.stub.CallService(request, timeout=DEFAULT_PROBE_TIMEOUT_S)
        except grpc.RpcError as exc:
            return pb.CallServiceResponse(success=False, error=str(exc))

    # -- actions ------------------------------------------------------------

    def _sync_actions(self, upstream: _Upstream) -> None:
        """Read the upstream's action list and route it into the merged graph."""
        try:
            resp = upstream.stub.ListActions(pb.ListActionsRequest())
        except Exception as exc:  # noqa: BLE001
            LOG.debug("cannot list actions on %s: %s", upstream.name, exc)
            return
        actions = {a.name: a.type for a in resp.actions if a.name}
        upstream.actions = actions
        with self._lock:
            if upstream.name == "sim":
                for name, type_name in actions.items():
                    self._actions.setdefault(name, (upstream.name, type_name))
            else:
                for name, type_name in actions.items():
                    self._actions[name] = (upstream.name, type_name)

    def _action_route(self, name: str) -> tuple[_Upstream, str] | None:
        with self._lock:
            entry = self._actions.get(name)
        if entry is None:
            return None
        owner, type_name = entry
        upstream = self.sim if owner == "sim" else self.real
        return upstream, type_name

    def split_goal_id(self, goal_id: str) -> tuple[_Upstream, str]:
        """Resolve a goal id this relay handed out back to its upstream.

        The relay prefixes every goal id it returns with the upstream that
        owns the action (``sim:<id>`` / ``real:<id>``). The two upstreams
        hand out ids of their own and nothing stops them from colliding, so
        the prefix is what keeps "follow this goal" unambiguous.
        """
        if ":" in goal_id:
            owner, _, real_id = goal_id.partition(":")
        else:
            owner, real_id = "sim", goal_id
        upstream = self.sim if owner == "sim" else self.real
        return upstream, real_id

    # -- downstream ---------------------------------------------------------

    def _serve(self, host: str, port: int) -> None:
        """Expose the merged graph over gRPC."""
        from ros2_bridge_sim.agent import BridgeAgent

        relay = self

        class RelayAgent(BridgeAgent):
            """The simulator's agent, with the relay's routing in front of it.

            Everything the simulator's agent already does - subscribing to
            the merged graph, publishing into it - is kept. What is added is
            the part that only makes sense for a *merge*: topics are counted
            from the upstreams, a publish goes back to the upstream that owns
            the topic, and services and actions are routed to the upstream
            that serves them.
            """

            def ListTopics(self, request, context):  # noqa: ANN001, N802
                resp = super().ListTopics(request, context)
                for topic in resp.topics:
                    type_name, publishers, subscribers = relay._counts.get(
                        topic.name, (topic.type, 0, 0))
                    topic.type = type_name or topic.type
                    topic.publisher_count = publishers
                    topic.subscriber_count = subscribers
                return resp

            def GetTopicInfo(self, request, context):  # noqa: ANN001, N802
                info = super().GetTopicInfo(request, context)
                type_name, publishers, subscribers = relay._counts.get(
                    info.name, (info.type, 0, 0))
                info.type = type_name or info.type
                info.publisher_count = publishers
                info.subscriber_count = subscribers
                return info

            def Publish(self, request, context):  # noqa: ANN001, N802
                encoding = (request.encoding or "cdr").strip().lower()
                if encoding != "cdr":
                    return pb.PublishResponse(
                        accepted=False,
                        error="unsupported encoding %r: only 'cdr' (or empty) is supported"
                              % request.encoding,
                    )
                if not request.topic:
                    return pb.PublishResponse(accepted=False, error="topic must not be empty")
                data = bytes(request.data)
                if not data:
                    return pb.PublishResponse(accepted=False, error="empty payload")
                type_name = request.type or relay.graph.topic_type(request.topic)
                if not type_name:
                    return pb.PublishResponse(
                        accepted=False,
                        error="no type known for topic %r; set 'type' in the request" % request.topic,
                    )
                relay._publish_back(request.topic, type_name, data)
                return pb.PublishResponse(accepted=True)

            # -- services --------------------------------------------------

            def ListServices(self, request, context):  # noqa: ANN001, N802
                resp = pb.ListServicesResponse()
                with relay._lock:
                    entries = dict(relay._services)
                for name, (owner, type_name) in sorted(entries.items()):
                    resp.services.append(
                        pb.ServiceInfo(name=name, type=type_name, source=owner))
                return resp

            def GetServiceInfo(self, request, context):  # noqa: ANN001, N802
                route = relay._service_route(request.name)
                if route is None:
                    context.abort(grpc.StatusCode.NOT_FOUND,
                                  "unknown service %r" % request.name)
                _upstream, type_name = route
                return pb.ServiceInfo(name=request.name, type=type_name, source="local")

            def CallService(self, request, context):  # noqa: ANN001, N802
                return relay._call_service(request, context)

            # -- actions ----------------------------------------------------

            def ListActions(self, request, context):  # noqa: ANN001, N802
                resp = pb.ListActionsResponse()
                with relay._lock:
                    entries = dict(relay._actions)
                for name, (owner, type_name) in sorted(entries.items()):
                    resp.actions.append(
                        pb.ActionInfo(name=name, type=type_name, source=owner))
                return resp

            def GetActionInfo(self, request, context):  # noqa: ANN001, N802
                route = relay._action_route(request.name)
                if route is None:
                    context.abort(grpc.StatusCode.NOT_FOUND,
                                  "unknown action %r" % request.name)
                _upstream, type_name = route
                return pb.ActionInfo(name=request.name, type=type_name, source="local")

            def SendActionGoal(self, request, context):  # noqa: ANN001, N802
                route = relay._action_route(request.name)
                if route is None:
                    context.abort(grpc.StatusCode.NOT_FOUND,
                                  "unknown action %r" % request.name)
                upstream, _type_name = route
                if upstream.stub is None:
                    return pb.SendActionGoalResponse(
                        accepted=False,
                        error="upstream %s is not reachable" % upstream.name)
                timeout = (request.timeout_ms / 1000.0) if request.timeout_ms > 0 \
                    else DEFAULT_ACTION_TIMEOUT_S
                # The upstream agent waits for its *own* action server with the
                # timeout in the request, not with the one this RPC was given.
                # A caller that left `timeout_ms` at zero would otherwise make
                # the upstream give up after its own five seconds, long
                # before the relay's budget ran out - the request then fails
                # with "no action server" even though the server is right
                # there. So the budget is put into the request as well.
                upstream_request = pb.SendActionGoalRequest()
                upstream_request.CopyFrom(request)
                upstream_request.timeout_ms = int(timeout * 1000)
                try:
                    resp = upstream.stub.SendActionGoal(upstream_request, timeout=timeout)
                except grpc.RpcError as exc:
                    return pb.SendActionGoalResponse(accepted=False, error=str(exc))
                # The goal id has to name the upstream as well: the two
                # upstreams hand out ids of their own, and the downstream has
                # to be able to tell them apart when it follows the goal.
                if resp.accepted and resp.goal_id:
                    resp.goal_id = "%s:%s" % (upstream.name, resp.goal_id)
                return resp

            def _goal_route(self, name: str, goal_id: str):
                """Which upstream owns a goal id handed out by this relay."""
                return relay.split_goal_id(goal_id)

            def ActionFeedback(self, request, context):  # noqa: ANN001, N802
                upstream, real_id = self._goal_route(request.name, request.goal_id)
                if upstream.stub is None:
                    context.abort(grpc.StatusCode.NOT_FOUND,
                                  "the upstream of goal %r is not reachable" % request.goal_id)
                upstream_request = pb.ActionFeedbackRequest(name=request.name, goal_id=real_id)
                try:
                    for message in upstream.stub.ActionFeedback(upstream_request):
                        message.goal_id = "%s:%s" % (upstream.name, message.goal_id)
                        yield message
                except grpc.RpcError as exc:
                    LOG.debug("action feedback for %s ended: %s", request.goal_id, exc.code())

            def GetActionResult(self, request, context):  # noqa: ANN001, N802
                upstream, real_id = self._goal_route(request.name, request.goal_id)
                if upstream.stub is None:
                    return pb.GetActionResultResponse(
                        success=False, error="the upstream of goal %r is not reachable"
                                           % request.goal_id)
                upstream_request = pb.GetActionResultRequest(
                    name=request.name, goal_id=real_id, timeout_ms=request.timeout_ms)
                timeout = (request.timeout_ms / 1000.0) if request.timeout_ms > 0 \
                    else DEFAULT_ACTION_TIMEOUT_S
                try:
                    return upstream.stub.GetActionResult(upstream_request, timeout=timeout)
                except grpc.RpcError as exc:
                    return pb.GetActionResultResponse(success=False, error=str(exc))

            def CancelActionGoal(self, request, context):  # noqa: ANN001, N802
                upstream, real_id = self._goal_route(request.name, request.goal_id)
                if upstream.stub is None:
                    return pb.CancelActionGoalResponse(
                        accepted=False, error="the upstream of goal %r is not reachable"
                                             % request.goal_id)
                upstream_request = pb.CancelActionGoalRequest(
                    name=request.name, goal_id=real_id)
                try:
                    return upstream.stub.CancelActionGoal(
                        upstream_request, timeout=DEFAULT_ACTION_TIMEOUT_S)
                except grpc.RpcError as exc:
                    return pb.CancelActionGoalResponse(accepted=False, error=str(exc))

        agent = RelayAgent(self.graph)

        # The worker pool has to be big enough for the *streaming* calls, not
        # just the unary ones: every `Subscribe` a downstream opens occupies
        # one worker for as long as it lives, and the daemon mirrors dozens of
        # topics at once. With a small pool the streams take every worker and
        # the unary calls (ListTopics, ListServices, ...) queue behind them -
        # the relay then stops answering entirely and the downstream gives up
        # with "Deadline Exceeded". So the pool is sized for the streams plus
        # a margin for the calls.
        executor = futures.ThreadPoolExecutor(max_workers=SERVER_MAX_WORKERS)
        server = grpc.server(executor)
        pb_grpc.add_Ros2BridgeServicer_to_server(agent, server)
        bound = server.add_insecure_port("%s:%d" % (host, port))
        if not bound:
            raise RuntimeError("could not bind %s:%d" % (host, port))
        server.ros2_bridge_executor = executor
        server.start()
        self._servers.append(server)
        LOG.info("relay serving %s:%d (merged graph)", host, bound)

    # -- lifecycle ----------------------------------------------------------

    def start(self) -> None:
        self.sim.connect()
        self.real.connect()
        for upstream in (self.sim, self.real):
            thread = threading.Thread(
                target=self._pump, args=(upstream,),
                name="pump-%s" % upstream.name, daemon=True,
            )
            thread.start()

    def serve(self, host: str, port: int, loopback_host: str = "", loopback_port: int = 0) -> None:
        """Serve on ``host:port`` and, when asked, also on the loopback pair.

        Serving on both is what lets the *same* configuration work on one
        machine (Windows reaches it over the LAN address, a local tool over
        127.0.0.1) and across two machines without a second set of files.

        A wildcard bind already accepts loopback connections, so the extra
        loopback server is only started when the main bind is a *specific*
        address. Starting it anyway would double the number of streaming
        workers for nothing.
        """
        wildcard = host in ("0.0.0.0", "::", "*", "")
        if (not wildcard and loopback_host and loopback_port
                and (loopback_host, loopback_port) != (host, port)):
            self._serve(loopback_host, loopback_port)
        self._serve(host, port)

    def stop(self) -> None:
        self._stop.set()
        for server in self._servers:
            server.stop(1.0).wait(timeout=5.0)
            executor = getattr(server, "ros2_bridge_executor", None)
            if executor is not None:
                executor.shutdown(wait=False)
        self._servers.clear()
        self.sim.close()
        self.real.close()

    def wait(self) -> None:
        while not self._stop.is_set():
            time.sleep(0.25)


def _make_graph():
    from ros2_bridge_sim.graph import Graph

    return Graph()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="ros2_link.relay",
        description="Relay one gRPC agent's graph into another's (sim <-> real ROS2).",
    )
    parser.add_argument("--sim", default="127.0.0.1:50051", help="sim agent address (host:port)")
    parser.add_argument("--real", default="127.0.0.1:50111", help="real ROS2 agent address (host:port)")
    parser.add_argument("--listen", default="0.0.0.0:50060", help="where to serve the merged graph")
    parser.add_argument("--loopback-listen", default="127.0.0.1:50060",
                        help="second bind on the loopback address (empty to skip)")
    parser.add_argument("--exclude", action="append", default=[],
                        help="topic to leave out (repeatable)")
    parser.add_argument("--collision", default="prefer-sim", choices=list(COLLISION_POLICIES),
                        help="who keeps the plain name when both sides publish it")
    parser.add_argument("--refresh", type=float, default=DEFAULT_REFRESH_S,
                        help="seconds between upstream topic-list refreshes")
    parser.add_argument("--log", default="", help="also write the log to this file")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args(argv)

    handlers: list[logging.Handler] = [logging.StreamHandler(sys.stderr)]
    if args.log:
        handlers.append(logging.FileHandler(args.log, encoding="utf-8"))
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
        handlers=handlers,
    )

    sim = _Upstream("sim", args.sim)
    real = _Upstream("real", args.real)
    exclude = DEFAULT_EXCLUDE + tuple(args.exclude)
    relay = Relay(sim, real, exclude=exclude, refresh_s=args.refresh,
                  collision=args.collision)

    host, _, port_s = args.listen.rpartition(":")
    host = host or "0.0.0.0"
    port = int(port_s)

    loop_host, loop_port = "", 0
    if args.loopback_listen:
        lh, _, lp = args.loopback_listen.rpartition(":")
        loop_host, loop_port = (lh or "127.0.0.1"), int(lp)

    def _stop(signum, frame):  # noqa: ANN001, ARG001
        LOG.info("signal %s: stopping", signum)
        relay.stop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, _stop)
        except (ValueError, OSError):
            pass

    relay.start()
    relay.serve(host, port, loop_host, loop_port)
    try:
        relay.wait()
    finally:
        relay.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
