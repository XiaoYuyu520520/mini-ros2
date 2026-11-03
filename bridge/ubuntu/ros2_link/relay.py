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
import hashlib
import logging
import math
import random
import signal
import sys
import threading
import time
import uuid
from collections import deque
from concurrent import futures

import grpc

from ros2_bridge_sim import messages as sim_messages
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

#: Topics that carry a velocity command. A publish on one of these makes a
#: robot move, so routing it by inference ("who is subscribed?") is not good
#: enough: when the route is ambiguous the relay refuses instead of falling
#: back to the simulator, which would silently drive the wrong robot.
DEFAULT_CONTROL_TOPICS = (
    "/cmd_vel",
    "/cmd_vel_nav",
    "/cmd_vel_raw",
    "/cmd_vel_smoothed",
)

#: How long an injected message is remembered for echo suppression. The agent
#: tags the copy it delivers back with the same ``origin_id``; this window only
#: has to cover the round trip, not the lifetime of the topic.
ECHO_TTL_S = 10.0

#: Upper bound on remembered injections, so a flood cannot grow the relay.
ECHO_MAX_PENDING = 4096

#: Re-subscription backoff. A stream that dies is retried with exponential
#: backoff plus jitter; a stream that lived long enough resets the counter, so
#: a flapping link settles instead of hot-looping.
SUB_BACKOFF_BASE_S = 0.5
SUB_BACKOFF_MAX_S = 30.0

#: A stream that stayed up at least this long counts as healthy even if it
#: ends later; its next failure starts the backoff over.
SUB_HEALTHY_AFTER_S = 2.0

#: How often expired echo/provenance entries are swept. Walking those dicts on
#: every message is fine at 20 Hz and wasteful at 20 kHz (a point cloud plus
#: two cameras), so the sweep is amortised and the per-message path only looks
#: at the entry it needs.
PRUNE_INTERVAL_S = 1.0

#: How often the relay logs its counters when idle.
STATS_INTERVAL_S = 60.0

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


class _Subscription:
    """One stream of one topic on one upstream, plus its retry state.

    The relay used to keep a bare ``thread`` per topic and skip a topic that
    already had one. That was wrong the moment the stream ended: the dead
    thread stayed in the dict and the topic was never subscribed again, so one
    network hiccup froze that half of the graph until the process restarted.
    The explicit state is what makes the recovery visible and testable.
    """

    #: No RPC yet, or a new one is being opened.
    CONNECTING = "CONNECTING"
    #: The stream is open and delivering.
    ACTIVE = "ACTIVE"
    #: The stream ended; waiting out the backoff before reconnecting.
    RETRYING = "RETRYING"
    #: Gave up - the topic is malformed for this agent (type conflict, ...).
    FAILED = "FAILED"

    __slots__ = ("upstream", "topic", "type_name", "thread", "call", "state",
                 "attempt", "retry_at", "started_at", "last_error", "delivered")

    def __init__(self, upstream: "_Upstream", topic: str, type_name: str) -> None:
        self.upstream = upstream
        self.topic = topic
        self.type_name = type_name
        self.thread: threading.Thread | None = None
        self.call = None
        self.state = self.CONNECTING
        self.attempt = 0
        self.retry_at = 0.0
        self.started_at = 0.0
        self.last_error = ""
        self.delivered = 0

    @property
    def alive(self) -> bool:
        return self.thread is not None and self.thread.is_alive()

    def backoff(self) -> float:
        """Seconds to wait before the next attempt, with jitter."""
        delay = min(SUB_BACKOFF_MAX_S, SUB_BACKOFF_BASE_S * (2 ** min(self.attempt, 6)))
        return delay * (0.5 + random.random())


class _PublishOutcome:
    """What became of one downstream publish.

    ``accepted`` is the wire field the daemon already reads; ``result`` is the
    finer distinction a caller acting on it needs (a command that timed out is
    not a command that reached a graph with no such topic).
    """

    __slots__ = ("accepted", "result", "error", "message_id")

    def __init__(self, *, accepted: bool, result: str, error: str = "",
                 message_id: str = "") -> None:
        self.accepted = accepted
        self.result = result
        self.error = error
        self.message_id = message_id

    def response(self) -> "pb.PublishResponse":
        return pb.PublishResponse(
            accepted=self.accepted, error=self.error,
            result=self.result, message_id=self.message_id,
        )

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return "_PublishOutcome(accepted=%r, result=%r, error=%r)" % (
            self.accepted, self.result, self.error)


class _Upstream:
    """One agent the relay talks to."""

    def __init__(self, name: str, addr: str, *, tls=None) -> None:  # noqa: ANN001
        self.name = name
        self.addr = addr
        self.host, self.port = _split_addr(addr)
        self.target = "%s:%d" % (self.host, self.port)
        #: optional client credentials (None = plaintext). The relay to the
        #: robot is the hop that can drive a car, so it is the one that most
        #: needs the option.
        self.tls = tls
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
        #: topic -> the subscription state machine currently streaming it
        self.subs: dict[str, _Subscription] = {}
        self.alive = False

    def connect(self) -> bool:
        """Open the channel and probe the agent."""
        try:
            if self.channel is None:
                if self.tls is not None:
                    self.channel = grpc.secure_channel(self.target, self.tls)
                else:
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
        for sub in list(self.subs.values()):
            if sub.call is not None:
                try:
                    sub.call.cancel()
                except Exception:  # noqa: BLE001 - best effort on shutdown
                    pass
        for sub in list(self.subs.values()):
            if sub.thread is not None:
                sub.thread.join(timeout=0.5)
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

    def __init__(
        self,
        sim: "_Upstream",
        real: "_Upstream",
        *,
        exclude: tuple[str, ...] = DEFAULT_EXCLUDE,
        refresh_s: float = DEFAULT_REFRESH_S,
        collision: str = "prefer-sim",
        control_routes: dict[str, str] | None = None,
        max_linear_speed: float | None = None,
        max_angular_speed: float | None = None,
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
        #: (upstream name, upstream topic) -> merged name. Once a name is
        #: chosen it stays, so a collision that is discovered later moves the
        #: loser aside instead of letting the two interleave during the window
        #: where one side has not been listed yet.
        self._stable: dict[tuple[str, str], str] = {}
        #: merged topic name -> (type, publisher_count, subscriber_count)
        self._counts: dict[str, tuple[str, int, int]] = {}
        #: service name -> (upstream name, service type)
        self._services: dict[str, tuple[str, str]] = {}
        #: action name -> (upstream name, action type)
        self._actions: dict[str, tuple[str, str]] = {}
        #: Echo suppression, protocol level. Every message this relay injects
        #: into an upstream carries an ``origin_id``; the agent hands that id
        #: back on the copy it delivers to us, so we recognise our own message
        #: instead of dropping whatever happens to arrive in the next 0.5 s.
        #: The fingerprint queue is the compatibility path for an agent that
        #: does not echo ids yet (it matches the exact bytes we sent).
        self.origin_id = "relay-%s" % uuid.uuid4().hex
        self._injection_seq = 0
        self._injections: dict[str, float] = {}
        self._echo_keys: dict[tuple[str, str], deque] = {}
        #: trace ids we have already handled, so a message crossing more than
        #: one relay hop is not merged twice.
        self._seen_traces: dict[str, float] = {}
        self._last_prune = 0.0
        #: The last topic snapshot per upstream. Comparing it with the next
        #: one is how a topic that disappeared is unregistered instead of
        #: lingering in ListTopics forever.
        self._upstream_topics: dict[str, set[str]] = {"sim": set(), "real": set()}
        #: The last service/action snapshot per upstream. The merged maps are
        #: rebuilt from these two, so a deleted service really disappears.
        self._upstream_services: dict[str, dict[str, str]] = {"sim": {}, "real": {}}
        self._upstream_actions: dict[str, dict[str, str]] = {"sim": {}, "real": {}}
        #: Coarse counters, the first half of the observability the next round
        #: needs: how much crossed, how much was dropped and why.
        self.stats = {
            "merged": 0, "dropped_echo": 0, "dropped_trace": 0,
            "reconnects": 0, "publish_accepted": 0, "publish_failed": 0,
        }
        #: merged topic name -> {received, merged, dropped_echo, bytes, last_stamp_ns}
        self.topic_stats: dict[str, dict[str, int]] = {}
        #: explicit control routing: merged topic -> "sim" | "real"
        self.control_routes = dict(control_routes or {})
        for topic, owner in self.control_routes.items():
            if owner not in ("sim", "real"):
                raise ValueError("control route for %r must be 'sim' or 'real', got %r"
                                 % (topic, owner))
        #: every topic whose publish makes a robot move
        self.control_topics = set(DEFAULT_CONTROL_TOPICS) | set(self.control_routes)
        #: optional safety clamps for Twist commands; None means "do not touch"
        self.max_linear_speed = max_linear_speed
        self.max_angular_speed = max_angular_speed
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

    def _merged_for(self, upstream_name: str, topic: str) -> str:
        """The merged name of one upstream topic, stable for the session."""
        key = (upstream_name, topic)
        with self._lock:
            existing = self._stable.get(key)
            if existing is not None:
                return existing
            merged = self._merged_name(upstream_name, topic)
            # The other side may already own this name because it was listed
            # first. A collision is a property of both graphs, not of the
            # order they came up in, so the loser is moved aside now.
            owner = self._route.get(merged)
            if owner is not None and owner != key:
                other_name, other_topic = owner
                displaced = "/%s/%s" % (other_name, other_topic.lstrip("/"))
                self._stable[(other_name, other_topic)] = displaced
                self._register_route(displaced, owner)
                LOG.info("collision on %s: %s is published as %s",
                         topic, other_name, displaced)
            self._stable[key] = merged
            self._register_route(merged, key)
            return merged

    def _register_route(self, merged: str, key: tuple[str, str]) -> None:
        """Point a merged name at an upstream topic."""
        self._route[merged] = key

    # -- echo suppression ---------------------------------------------------

    def _prune_echo(self, now: float) -> None:
        """Sweep expired provenance at most once per PRUNE_INTERVAL_S.

        The old version walked every dict for every message. That is a real
        cost once a point cloud, two cameras and several robots share the
        process, and the sweep does not need to be that precise: an entry that
        lives an extra second after its deadline is harmless because every
        lookup also checks the deadline.
        """
        if now - self._last_prune < PRUNE_INTERVAL_S:
            return
        self._last_prune = now
        for origin_id, deadline in list(self._injections.items()):
            if deadline <= now:
                del self._injections[origin_id]
        for key, queue in list(self._echo_keys.items()):
            while queue and queue[0][1] <= now:
                queue.popleft()
            if not queue:
                del self._echo_keys[key]
        for trace_id, deadline in list(self._seen_traces.items()):
            if deadline <= now:
                del self._seen_traces[trace_id]

    @staticmethod
    def _fingerprint(data: bytes) -> bytes:
        return hashlib.sha256(data).digest()

    def _remember_injection(self, upstream_name: str, topic: str, data: bytes,
                            origin_id: str) -> None:
        """Remember one message we are about to inject into an upstream."""
        now = time.monotonic()
        with self._lock:
            self._prune_echo(now)
            self._injections[origin_id] = now + ECHO_TTL_S
            queue = self._echo_keys.setdefault((upstream_name, topic), deque())
            queue.append((self._fingerprint(data), now + ECHO_TTL_S))
            total = sum(len(q) for q in self._echo_keys.values())
            while total > ECHO_MAX_PENDING and queue:
                queue.popleft()
                total -= 1
            if not queue:
                self._echo_keys.pop((upstream_name, topic), None)

    def _forget_injection(self, upstream_name: str, topic: str, data: bytes,
                          origin_id: str) -> None:
        """Drop the bookkeeping for an injection that will never echo."""
        fingerprint = self._fingerprint(data)
        with self._lock:
            self._injections.pop(origin_id, None)
            queue = self._echo_keys.get((upstream_name, topic))
            if queue:
                for index, (stored, _deadline) in enumerate(queue):
                    if stored == fingerprint:
                        del queue[index]
                        break
                if not queue:
                    self._echo_keys.pop((upstream_name, topic), None)

    def _consume_fingerprint(self, upstream_name: str, topic: str, data: bytes) -> bool:
        """True when these exact bytes are one of our own injections."""
        now = time.monotonic()
        fingerprint = self._fingerprint(data)
        with self._lock:
            self._prune_echo(now)
            queue = self._echo_keys.get((upstream_name, topic))
            if not queue:
                return False
            for index, (stored, deadline) in enumerate(queue):
                if deadline > now and stored == fingerprint:
                    del queue[index]
                    if not queue:
                        self._echo_keys.pop((upstream_name, topic), None)
                    return True
            return False

    def _is_echo(self, upstream: "_Upstream", topic: str, message) -> bool:  # noqa: ANN001
        """Is this upstream message the copy of one the relay just injected?

        Identity first: an ``origin_id`` is only ours if it is in the
        injection table, and the agent's ``source_id`` proves the far end can
        tag echoes at all. The payload hash is a compatibility path for agents
        older than the provenance fields - two independent publishers that
        happen to send identical bytes must not be mistaken for each other,
        which is exactly what a hash-only rule would do.
        """
        origin_id = getattr(message, "origin_id", "")
        if origin_id:
            with self._lock:
                known = self._injections.pop(origin_id, None)
            if known is not None:
                # The agent echoed our id: exact match, and the fingerprint
                # slot is no longer needed.
                self._consume_fingerprint(upstream.name, topic, message.data)
                return True
            # A foreign origin id: another relay (or another process) sent
            # this. It is real data, never an echo of ours.
            return False
        if getattr(message, "source_id", ""):
            # A new agent stamps its identity on everything it forwards. No
            # origin id from such an agent means the message did not come from
            # an injection, so do not fall back to the hash.
            return False
        return self._consume_fingerprint(upstream.name, topic, message.data)

    def _seen_trace(self, trace_id: str) -> bool:
        """True when this trace id already crossed this relay."""
        if not trace_id:
            return False
        now = time.monotonic()
        with self._lock:
            self._prune_echo(now)
            if self._seen_traces.get(trace_id, 0.0) > now:
                return True
            self._seen_traces[trace_id] = now + ECHO_TTL_S
            return False

    def stats_snapshot(self) -> dict:
        """A consistent copy of the counters, for logs and tests."""
        with self._lock:
            return {
                "counts": dict(self.counts),
                "global": dict(self.stats),
                "topics": {name: dict(values) for name, values in self.topic_stats.items()},
                "subscriptions": {
                    upstream.name: {
                        topic: {
                            "state": sub.state,
                            "attempt": sub.attempt,
                            "delivered": sub.delivered,
                            "last_error": sub.last_error,
                        }
                        for topic, sub in upstream.subs.items()
                    }
                    for upstream in (self.sim, self.real)
                },
            }

    # -- the merged graph ---------------------------------------------------

    def _publish_merged(self, topic: str, type_name: str, data: bytes, stamp_ns: int,
                        origin: str, trace_id: str = "") -> None:
        """Put a message from one upstream into the merged graph."""
        if topic in self.exclude:
            return
        if self._seen_trace(trace_id):
            with self._lock:
                self.stats["dropped_trace"] += 1
            return
        try:
            self.graph.publish(topic, type_name, data, stamp_ns=stamp_ns, trace_id=trace_id)
        except Exception as exc:  # noqa: BLE001 - one bad message must not kill the relay
            LOG.warning("cannot merge %s from %s: %s", topic, origin, exc)
            return
        with self._lock:
            self.counts["%s->down" % origin] = self.counts.get("%s->down" % origin, 0) + 1
            self.stats["merged"] += 1
            topic_stat = self.topic_stats.setdefault(topic, {
                "received": 0, "merged": 0, "dropped_echo": 0,
                "bytes": 0, "last_stamp_ns": 0,
            })
            topic_stat["merged"] += 1

    def _origin_of(self, topic: str) -> str:
        with self._lock:
            route = self._route.get(topic)
        return route[0] if route else "sim"

    # -- upstream -> merged -------------------------------------------------

    def _stream_topic(self, sub: "_Subscription") -> None:
        """Subscribe to one topic on one upstream and merge what comes.

        The state transitions here are the ones ``_pump`` reasons about: a
        stream that ends leaves ``RETRYING`` with a backoff deadline, and the
        pump starts a new thread for it. Without that the dict entry would
        keep a dead thread and the topic would never be subscribed again.
        """
        upstream = sub.upstream
        assert upstream.stub is not None
        request = pb.SubscribeRequest(topic=sub.topic, type=sub.type_name)
        sub.state = _Subscription.CONNECTING
        sub.started_at = time.monotonic()
        try:
            call = upstream.stub.Subscribe(request)
            sub.call = call
            # The RPC object exists as soon as the call is opened; a failure
            # surfaces from the iterator below. The state is ACTIVE, but the
            # failure counter is deliberately *not* reset here: a stream that
            # dies immediately must keep climbing the backoff ladder. Only a
            # stream that survives SUB_HEALTHY_AFTER_S resets it (in `finally`).
            sub.state = _Subscription.ACTIVE
            for message in call:
                if self._stop.is_set():
                    return
                sub.delivered += 1
                upstream_topic = message.topic or sub.topic
                merged = self._merged_for(upstream.name, upstream_topic)
                with self._lock:
                    topic_stat = self.topic_stats.setdefault(merged, {
                        "received": 0, "merged": 0, "dropped_echo": 0,
                        "bytes": 0, "last_stamp_ns": 0,
                    })
                    topic_stat["received"] += 1
                    topic_stat["bytes"] += len(message.data)
                    topic_stat["last_stamp_ns"] = message.stamp_ns
                if self._is_echo(upstream, upstream_topic, message):
                    with self._lock:
                        self.stats["dropped_echo"] += 1
                        topic_stat["dropped_echo"] += 1
                    continue
                self._publish_merged(merged, message.type or sub.type_name,
                                     message.data, message.stamp_ns,
                                     upstream.name, getattr(message, "trace_id", ""))
        except grpc.RpcError as exc:
            if not self._stop.is_set():
                sub.last_error = str(exc.code())
                if exc.code() in (grpc.StatusCode.INVALID_ARGUMENT,
                                  grpc.StatusCode.FAILED_PRECONDITION):
                    # A type conflict is not a transient network problem, but
                    # the topic can be re-created with the right type; retry
                    # slowly instead of hot-looping.
                    sub.state = _Subscription.FAILED
        except Exception as exc:  # noqa: BLE001
            if not self._stop.is_set():
                sub.last_error = str(exc)
                LOG.warning("subscription on %s/%s failed: %s",
                            upstream.name, sub.topic, exc)
        finally:
            sub.call = None
            if self._stop.is_set():
                return
            now = time.monotonic()
            if now - sub.started_at >= SUB_HEALTHY_AFTER_S:
                sub.attempt = 0
            sub.attempt += 1
            if sub.state != _Subscription.FAILED:
                sub.state = _Subscription.RETRYING
            sub.retry_at = now + sub.backoff()
            with self._lock:
                self.stats["reconnects"] += 1
            LOG.info("subscription on %s/%s ended (%s); retry #%d in %.1fs",
                     upstream.name, sub.topic, sub.last_error or "closed",
                     sub.attempt, sub.retry_at - now)

    def _cancel_streams(self, upstream: "_Upstream") -> None:
        """Cancel every stream of an upstream; the pump restarts them."""
        for sub in list(upstream.subs.values()):
            if sub.call is not None:
                try:
                    sub.call.cancel()
                except Exception:  # noqa: BLE001 - cancelling is best effort
                    pass

    def _pump(self, upstream: "_Upstream") -> None:
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
                # The agent is gone or the channel broke: cancel the streams
                # (they would hang on the dead channel) and drop the stub so
                # the next round reconnects.
                self._cancel_streams(upstream)
                upstream.alive = False
                upstream.stub = None
                time.sleep(self.refresh_s)
                continue

            upstream.topics = topics
            upstream.all_topics = all_topics

            # Unregister topics this upstream no longer has. Without this the
            # merged graph kept listing a node's old topics forever: the old
            # code cancelled the stream but left `_route`/`_counts` in place,
            # so a restarted node with a different type was shadowed by the
            # stale entry.
            current_names = set(topics)
            with self._lock:
                removed = self._upstream_topics.get(upstream.name, set()) - current_names
                for name in removed:
                    key = (upstream.name, name)
                    merged = self._stable.pop(key, None)
                    if merged is not None and self._route.get(merged) == key:
                        del self._route[merged]
                        self._counts.pop(merged, None)
                        self.topic_stats.pop(merged, None)
                self._upstream_topics[upstream.name] = current_names

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
            for name, (type_name, publishers, subscribers) in topics.items():
                merged = self._merged_for(upstream.name, name)
                if merged in self.exclude:
                    continue
                with self._lock:
                    self._counts[merged] = (type_name, publishers, subscribers)
                try:
                    self.graph._ensure(merged, type_name)
                except Exception as exc:  # noqa: BLE001
                    LOG.debug("cannot register %s in the merged graph: %s", merged, exc)

            # stop streaming topics the agent no longer has
            for name in list(upstream.subs):
                if name not in topics:
                    sub = upstream.subs.pop(name)
                    if sub.call is not None:
                        try:
                            sub.call.cancel()
                        except Exception:  # noqa: BLE001
                            pass

            # start (or restart) the streams that should be running
            now = time.monotonic()
            for name, (type_name, _pub, _sub) in topics.items():
                if name in self.exclude:
                    continue
                sub = upstream.subs.get(name)
                if sub is None:
                    sub = _Subscription(upstream, name, type_name)
                    upstream.subs[name] = sub
                elif sub.type_name != type_name:
                    sub.type_name = type_name
                if sub.alive:
                    continue
                if now < sub.retry_at:
                    continue
                sub.thread = threading.Thread(
                    target=self._stream_topic,
                    args=(sub,),
                    name="sub-%s-%s" % (upstream.name, name),
                    daemon=True,
                )
                sub.thread.start()

            time.sleep(self.refresh_s)

    # -- merged -> upstream -------------------------------------------------

    def _control_target(self, topic: str, upstream_topic: str) -> tuple["_Upstream | None", str]:
        """Where a control command must go, or None when it is ambiguous.

        An explicit route always wins. Otherwise a control topic is sent to
        the single upstream that only *subscribes* to it (the actor). If there
        is no such upstream, or both sides consume it, the relay refuses: a
        velocity command must never silently land on the simulator just
        because the real robot is not connected.
        """
        explicit = self.control_routes.get(topic)
        if explicit is not None:
            return (self.sim if explicit == "sim" else self.real), explicit
        # No explicit route: infer only while *both* upstreams are known to be
        # up. A command sent while the real robot is unreachable must be
        # refused, not quietly delivered to whichever upstream happens to be
        # left - that is how a simulator (or the wrong robot) starts moving.
        if not (self.sim.alive and self.real.alive):
            return None, ""
        sinks = []
        for upstream in (self.sim, self.real):
            info = upstream.all_topics.get(upstream_topic)
            if info is not None and info[1] == 0 and info[2] > 0:
                sinks.append(upstream)
        if len(sinks) == 1:
            return sinks[0], sinks[0].name
        return None, ""

    def _apply_speed_limits(self, topic: str, type_name: str,
                            data: bytes) -> tuple[bytes, str]:
        """Clamp a Twist command when limits were configured.

        Only ``geometry_msgs/msg/Twist`` is clamped; for anything else the
        bytes are forwarded untouched rather than risking a wrong re-encode.
        The robot still needs its own watchdog: a link that dies cannot send
        the stop command this clamp would otherwise produce.
        """
        if topic not in self.control_topics:
            return data, ""
        if self.max_linear_speed is None and self.max_angular_speed is None:
            return data, ""
        if type_name != "geometry_msgs/msg/Twist":
            return data, ""
        try:
            decoded = sim_messages.decode(data, type_name)
        except Exception as exc:  # noqa: BLE001 - refuse rather than clamp blind
            return data, "cannot decode Twist for speed limiting: %s" % exc
        linear = decoded.get("linear") or {}
        angular = decoded.get("angular") or {}
        vx = float(linear.get("x", 0.0))
        vy = float(linear.get("y", 0.0))
        vz = float(linear.get("z", 0.0))
        if self.max_linear_speed is not None:
            speed = math.sqrt(vx * vx + vy * vy + vz * vz)
            if self.max_linear_speed > 0 and speed > self.max_linear_speed:
                scale = self.max_linear_speed / speed
                linear.update(x=vx * scale, y=vy * scale, z=vz * scale)
        if self.max_angular_speed is not None:
            wz = float(angular.get("z", 0.0))
            if abs(wz) > self.max_angular_speed:
                angular["z"] = math.copysign(self.max_angular_speed, wz)
        decoded["linear"] = linear
        decoded["angular"] = angular
        try:
            return sim_messages.encode(decoded, type_name), ""
        except Exception as exc:  # noqa: BLE001
            return data, "cannot re-encode clamped Twist: %s" % exc

    def _publish_back(self, topic: str, type_name: str, data: bytes,
                      *, trace_id: str = "") -> "_PublishOutcome":
        """Send a message that came from downstream back to where it acts.

        "Back to its origin" is the wrong rule for a command. ``/cmd_vel`` is
        published by the *real* ROS2 graph (nav2's velocity smoother) and
        consumed by the *simulator* (the car). A command published from
        downstream means "act on this", so it has to go to the side that
        consumes it - otherwise the Windows controller drives the real robot's
        output topic and the car never moves.

        For a control topic the destination is never guessed when it is
        ambiguous: an explicit ``control_routes`` entry wins, otherwise there
        must be exactly one upstream that only consumes the topic. Anything
        else is refused, so a missing real robot cannot make the simulator
        move by accident. A non-control topic keeps the old inference: sink,
        then the topic's owner, then the simulator.
        """
        with self._lock:
            route = self._route.get(topic)
        upstream_topic = route[1] if route is not None else topic

        target: _Upstream | None = None
        owner = ""
        if topic in self.control_topics:
            target, owner = self._control_target(topic, upstream_topic)
            if target is None:
                return _PublishOutcome(
                    accepted=False, result="REJECTED",
                    error=("control route for %r is ambiguous (%s); configure an "
                           "explicit control route before sending a velocity command"
                           % (topic, self._describe_sinks(upstream_topic))))
        else:
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
                # still writable from downstream. It goes to the simulator,
                # which is the side that acts on a command.
                owner, target = "sim", self.sim

        if target.stub is None:
            return _PublishOutcome(
                accepted=False, result="UNAVAILABLE",
                error="upstream %s is not reachable" % target.name)

        data, clamp_error = self._apply_speed_limits(topic, type_name, data)
        if clamp_error:
            return _PublishOutcome(accepted=False, result="REJECTED", error=clamp_error)

        with self._lock:
            self._injection_seq += 1
            origin_id = "%s:%d" % (self.origin_id, self._injection_seq)
        self._remember_injection(target.name, upstream_topic, data, origin_id)
        try:
            response = target.stub.Publish(
                pb.PublishRequest(
                    topic=upstream_topic, type=type_name, data=data,
                    encoding="cdr", origin_id=origin_id, trace_id=trace_id,
                ),
                timeout=DEFAULT_PROBE_TIMEOUT_S,
            )
        except grpc.RpcError as exc:
            self._forget_injection(target.name, upstream_topic, data, origin_id)
            code = exc.code() if hasattr(exc, "code") else None
            if code == grpc.StatusCode.DEADLINE_EXCEEDED:
                result = "TIMEOUT"
            elif code in (grpc.StatusCode.UNAVAILABLE, grpc.StatusCode.CANCELLED):
                result = "UNAVAILABLE"
            else:
                result = "REJECTED"
            with self._lock:
                self.stats["publish_failed"] += 1
            LOG.warning("cannot publish %s back to %s (%s): %s", topic, owner, result, exc)
            return _PublishOutcome(accepted=False, result=result, error=str(exc))
        except Exception as exc:  # noqa: BLE001
            self._forget_injection(target.name, upstream_topic, data, origin_id)
            LOG.warning("cannot publish %s back to %s: %s", topic, owner, exc)
            return _PublishOutcome(accepted=False, result="REJECTED", error=str(exc))

        if not response.accepted:
            # No echo is coming for a refused publish; release the bookkeeping
            # now so an identical legitimate message is not dropped later.
            self._forget_injection(target.name, upstream_topic, data, origin_id)
            result = response.result or "REJECTED"
            if result not in ("REJECTED", "UNAVAILABLE", "TIMEOUT"):
                result = "REJECTED"
            return _PublishOutcome(
                accepted=False, result=result,
                error=response.error or "upstream refused the message")

        self.counts["down->%s" % owner] = self.counts.get("down->%s" % owner, 0) + 1
        with self._lock:
            self.stats["publish_accepted"] += 1
        result = response.result or "ACCEPTED"
        if result not in ("ACCEPTED", "REJECTED", "UNAVAILABLE", "TIMEOUT"):
            result = "ACCEPTED"
        return _PublishOutcome(accepted=True, result=result,
                               message_id=response.message_id)

    def _describe_sinks(self, upstream_topic: str) -> str:
        parts = []
        for upstream in (self.sim, self.real):
            info = upstream.all_topics.get(upstream_topic)
            if info is None:
                parts.append("%s: absent" % upstream.name)
            elif info[1] == 0 and info[2] > 0:
                parts.append("%s: consumer" % upstream.name)
            elif info[1] > 0:
                parts.append("%s: publisher" % upstream.name)
            else:
                parts.append("%s: idle" % upstream.name)
        return ", ".join(parts)
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
            self._upstream_services[upstream.name] = dict(services)
            self._rebuild_services_locked()

    def _rebuild_services_locked(self) -> None:
        """Recompute the merged service map from the two snapshots.

        The simulator is the fallback and the real ROS2 graph wins a name
        collision; both are now *snapshots*, so a service the node stopped
        advertising disappears instead of being merged forever.
        """
        merged: dict[str, tuple[str, str]] = {}
        for name, type_name in self._upstream_services.get("sim", {}).items():
            merged.setdefault(name, ("sim", type_name))
        for name, type_name in self._upstream_services.get("real", {}).items():
            merged[name] = ("real", type_name)
        self._services = merged

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
            self._upstream_actions[upstream.name] = dict(actions)
            self._rebuild_actions_locked()

    def _rebuild_actions_locked(self) -> None:
        """Recompute the merged action map from the two snapshots."""
        merged: dict[str, tuple[str, str]] = {}
        for name, type_name in self._upstream_actions.get("sim", {}).items():
            merged.setdefault(name, ("sim", type_name))
        for name, type_name in self._upstream_actions.get("real", {}).items():
            merged[name] = ("real", type_name)
        self._actions = merged

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

    def _serve(self, host: str, port: int, credentials=None) -> None:  # noqa: ANN001
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
                # Only topics that are still in an upstream snapshot. A
                # graph topic can outlive its source when it still has a
                # downstream subscriber, and listing it would make a dead
                # node look alive.
                with relay._lock:
                    active = set(relay._counts)
                    counts = dict(relay._counts)
                stale = [topic for topic in resp.topics if topic.name not in active]
                for topic in stale:
                    resp.topics.remove(topic)
                for topic in resp.topics:
                    type_name, publishers, subscribers = counts.get(
                        topic.name, (topic.type, 0, 0))
                    topic.type = type_name or topic.type
                    topic.publisher_count = publishers
                    topic.subscriber_count = subscribers
                return resp

            def GetTopicInfo(self, request, context):  # noqa: ANN001, N802
                info = super().GetTopicInfo(request, context)
                with relay._lock:
                    entry = relay._counts.get(info.name)
                if entry is None:
                    context.abort(grpc.StatusCode.NOT_FOUND,
                                  "unknown topic %r" % info.name)
                type_name, publishers, subscribers = entry
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
                        result="REJECTED",
                    )
                if not request.topic:
                    return pb.PublishResponse(accepted=False,
                                              error="topic must not be empty",
                                              result="REJECTED")
                data = bytes(request.data)
                if not data:
                    return pb.PublishResponse(accepted=False, error="empty payload",
                                              result="REJECTED")
                type_name = request.type or relay.graph.topic_type(request.topic)
                if not type_name:
                    return pb.PublishResponse(
                        accepted=False,
                        error="no type known for topic %r; set 'type' in the request" % request.topic,
                        result="REJECTED",
                    )
                # The outcome is what the caller is told. Reporting
                # `accepted=True` for a command that never left this process is
                # how a Windows controller concludes the car moved when the
                # real robot was not even connected - see `_publish_back`.
                outcome = relay._publish_back(
                    request.topic, type_name, data,
                    trace_id=getattr(request, "trace_id", ""))
                return outcome.response()

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
                    # A broken feedback stream is not a normal end. Passing
                    # the status on is what lets the caller tell "the goal
                    # finished" from "the link died while it was running"; a
                    # debug log here made both look like success.
                    if exc.code() == grpc.StatusCode.CANCELLED or relay._stop.is_set():
                        LOG.debug("action feedback for %s cancelled", request.goal_id)
                        return
                    LOG.warning("action feedback for %s failed: %s",
                                request.goal_id, exc.code())
                    context.abort(exc.code(),
                                  "action feedback for %s ended: %s"
                                  % (request.goal_id, exc.details()))

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
        if credentials is not None:
            bound = server.add_secure_port("%s:%d" % (host, port), credentials)
        else:
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

    def serve(self, host: str, port: int, loopback_host: str = "", loopback_port: int = 0,
              credentials=None) -> None:  # noqa: ANN001
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
            self._serve(loopback_host, loopback_port, credentials)
        self._serve(host, port, credentials)

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
        next_report = time.monotonic() + STATS_INTERVAL_S
        while not self._stop.is_set():
            if time.monotonic() >= next_report:
                next_report += STATS_INTERVAL_S
                snapshot = self.stats_snapshot()
                LOG.info("relay stats: global=%s topics=%d subscriptions=%s",
                         snapshot["global"], len(snapshot["topics"]),
                         {name: len(subs) for name, subs in
                          snapshot["subscriptions"].items()})
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
    parser.add_argument("--control", action="append", default=[], metavar="TOPIC=sim|real",
                        help="explicit route for a velocity command (repeatable); "
                             "a control topic without a route must have exactly one "
                             "consumer or the publish is refused")
    parser.add_argument("--max-linear-speed", type=float, default=None,
                        help="clamp Twist linear speed (m/s) on control topics; "
                             "unset means no clamp - set it from the chassis spec")
    parser.add_argument("--max-angular-speed", type=float, default=None,
                        help="clamp Twist angular z (rad/s) on control topics")
    parser.add_argument("--tls-root", default="",
                        help="CA certificate for TLS to the upstream agents and for "
                             "serving TLS to the downstream (empty = plaintext)")
    parser.add_argument("--tls-cert", default="",
                        help="server certificate for the downstream (PEM path)")
    parser.add_argument("--tls-key", default="",
                        help="server private key for the downstream (PEM path)")
    parser.add_argument("--tls-require-client-cert", action="store_true",
                        help="require a client certificate from the downstream (mTLS)")
    parser.add_argument("--log", default="", help="also write the log to this file")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args(argv)

    control_routes: dict[str, str] = {}
    for spec in args.control:
        topic, sep, owner = spec.rpartition("=")
        if not sep or not topic or owner not in ("sim", "real"):
            parser.error("--control expects TOPIC=sim|real, got %r" % spec)
        control_routes[topic] = owner

    handlers: list[logging.Handler] = [logging.StreamHandler(sys.stderr)]
    if args.log:
        handlers.append(logging.FileHandler(args.log, encoding="utf-8"))
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
        handlers=handlers,
    )

    # TLS is opt-in and applies to both directions. The hop that can drive a
    # car should not be plaintext outside a trusted LAN, but the lab default
    # stays plaintext so existing scripts keep working.
    client_tls = None
    server_credentials = None
    if args.tls_root or args.tls_cert:
        if not args.tls_root:
            parser.error("--tls-cert/--tls-key need --tls-root")
        with open(args.tls_root, encoding="utf-8") as handle:
            root_pem = handle.read()
        client_tls = grpc.ssl_channel_credentials(root_certificates=root_pem.encode())
        if args.tls_cert or args.tls_key:
            if not (args.tls_cert and args.tls_key):
                parser.error("--tls-cert and --tls-key must be given together")
            with open(args.tls_cert, encoding="utf-8") as handle:
                cert_pem = handle.read()
            with open(args.tls_key, encoding="utf-8") as handle:
                key_pem = handle.read()
            if args.tls_require_client_cert:
                server_credentials = grpc.ssl_server_credentials(
                    [(key_pem.encode(), cert_pem.encode())],
                    root_certificates=root_pem.encode(),
                    require_client_auth=True,
                )
            else:
                server_credentials = grpc.ssl_server_credentials(
                    [(key_pem.encode(), cert_pem.encode())])

    sim = _Upstream("sim", args.sim, tls=client_tls)
    real = _Upstream("real", args.real, tls=client_tls)
    exclude = DEFAULT_EXCLUDE + tuple(args.exclude)
    relay = Relay(sim, real, exclude=exclude, refresh_s=args.refresh,
                  collision=args.collision,
                  control_routes=control_routes,
                  max_linear_speed=args.max_linear_speed,
                  max_angular_speed=args.max_angular_speed)

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
    relay.serve(host, port, loop_host, loop_port, server_credentials)
    try:
        relay.wait()
    finally:
        relay.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
