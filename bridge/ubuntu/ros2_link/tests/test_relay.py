"""The relay must merge two independent agent graphs into one.

The scenario is the one the connector exists for: a genuine ROS2 graph and
the simulator, each behind its own gRPC agent, and a Windows daemon that only
ever sees *one* agent.

What is asserted here:

* the merged graph carries the topics of **both** sides;
* a message published on the simulator agent reaches a subscriber of the
  merged graph, and so does a message published on the real-ROS2 agent;
* a message published *through* the relay (what the Windows daemon does) is
  delivered back to the upstream it came from, and is **not** echoed to the
  relay's own subscribers - that echo would be a feedback loop, and it is the
  one thing the relay can get quietly wrong;
* a topic that **both** sides publish is not merged into one topic: the two
  robots' data would interleave, so the loser is published under a prefixed
  name of its own and no data is lost;
* **services** are advertised from both sides and a call is routed to the
  upstream that serves it;
* **actions** are advertised from both sides, a goal is routed to the
  upstream that owns it, and the feedback follows.

A topic that only *subscribes* on an upstream (the simulator's ``/cmd_vel``)
is a sink, not a source: the relay must not mirror it, or every command
would come straight back.
"""

from __future__ import annotations

import os
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import grpc  # noqa: E402

from ros2_bridge_sim import messages as msg  # noqa: E402
from ros2_bridge_sim.agent import build_server, stop_server  # noqa: E402
from ros2_bridge_sim.graph import Graph  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402
from ros2_link.relay import (  # noqa: E402
    SUB_BACKOFF_MAX_S, Relay, _Subscription, _Upstream,
)

TIMEOUT = 5.0


def _wait_for(predicate, timeout: float = TIMEOUT) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.02)
    return False


def _free_port() -> int:
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def _scan_payload(marker: float = 1.0) -> bytes:
    return msg.encode(
        {"header": {"stamp": {"sec": 1, "nanosec": 0}, "frame_id": "laser"},
         "angle_min": 0.0, "angle_max": 1.0, "angle_increment": 0.1,
         "time_increment": 0.0, "scan_time": 0.05, "range_min": 0.1,
         "range_max": 12.0, "ranges": [marker], "intensities": []},
        msg.LASER_SCAN)


def _scan_marker(message) -> float:  # noqa: ANN001 - protobuf Message
    return msg.decode(message.data, msg.LASER_SCAN)["ranges"][0]


def _twist(x: float) -> bytes:
    return msg.encode(
        {"linear": {"x": x, "y": 0.0, "z": 0.0},
         "angular": {"x": 0.0, "y": 0.0, "z": 0.0}}, msg.TWIST)


def _collect(stream, sink):
    try:
        for message in stream:
            sink.append(message)
    except grpc.RpcError:
        pass  # the channel closes at teardown; not a failure


class RelayTest(unittest.TestCase):
    def setUp(self) -> None:
        self.servers = []
        self.relay = None

    def tearDown(self) -> None:
        if self.relay is not None:
            self.relay.stop()
        for server, _agent, _port in self.servers:
            stop_server(server)

    def _agent(self, graph: Graph):
        server, agent, port = build_server(graph, "127.0.0.1", 0)
        server.start()
        self.servers.append((server, agent, port))
        return agent, port

    def _relay(self, sim_graph: Graph, real_graph: Graph, collision: str = "prefer-sim",
               **kwargs):
        sim_agent, sim_port = self._agent(sim_graph)
        real_agent, real_port = self._agent(real_graph)
        self.sim_agent, self.real_agent = sim_agent, real_agent
        sim = _Upstream("sim", "127.0.0.1:%d" % sim_port)
        real = _Upstream("real", "127.0.0.1:%d" % real_port)
        self.relay = Relay(sim, real, refresh_s=0.2, collision=collision, **kwargs)
        self.relay.start()
        relay_port = _free_port()
        self.relay.serve("127.0.0.1", relay_port, "", 0)
        channel = grpc.insecure_channel("127.0.0.1:%d" % relay_port)
        self.addCleanup(channel.close)
        return pb_grpc.Ros2BridgeStub(channel), sim, real

    def test_relay_merges_two_graphs(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()

        # The simulator publishes /scan and only *subscribes* to /cmd_vel.
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)
        sim_cmd = sim_graph.create_subscription("/cmd_vel", msg.TWIST, lambda event: None)
        # The real ROS2 graph publishes /chatter.
        real_graph.create_publisher("/chatter", msg.STRING)

        stub, sim, real = self._relay(sim_graph, real_graph)

        # 1. The merged graph lists the topics of both sides - and only
        #    the ones somebody publishes.
        self.assertTrue(
            _wait_for(lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
                      >= {"/scan", "/chatter"}),
            "the merged graph should list both sides' topics",
        )
        names = {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
        self.assertIn("/scan", names)
        self.assertIn("/chatter", names)
        self.assertNotIn("/cmd_vel", names,
                         "a topic with no publisher is a sink, not a source")

        # 2. Messages from both sides reach a subscriber of the merged graph.
        received: list = []
        chatter: list = []

        stream = stub.Subscribe(pb.SubscribeRequest(topic="/scan", type=msg.LASER_SCAN))
        threading.Thread(target=_collect, args=(stream, received), daemon=True).start()

        chat_stream = stub.Subscribe(pb.SubscribeRequest(topic="/chatter", type=msg.STRING))
        threading.Thread(target=_collect, args=(chat_stream, chatter), daemon=True).start()

        # Wait for the relay to have subscribed to both upstreams *and* for
        # the merged graph to have our downstream subscribers attached -
        # otherwise the first message can be merged before anyone listens.
        self.assertTrue(_wait_for(lambda: "/scan" in sim.topics and "/chatter" in real.topics),
                        "the relay should have subscribed to both upstreams")
        self.assertTrue(
            _wait_for(lambda: (self.relay.graph.get_topic("/scan") or
                               type("T", (), {"subscriber_count": 0})).subscriber_count >= 1
                      and (self.relay.graph.get_topic("/chatter") or
                           type("T", (), {"subscriber_count": 0})).subscriber_count >= 1),
            "the merged graph should have downstream subscribers attached",
        )

        sim_graph.publish("/scan", msg.LASER_SCAN, _scan_payload())
        real_graph.publish("/chatter", msg.STRING, msg.encode({"data": "hello"}, msg.STRING))

        self.assertTrue(_wait_for(lambda: len(received) >= 1),
                        "the simulator's /scan should reach the merged graph")
        self.assertTrue(_wait_for(lambda: len(chatter) >= 1),
                        "the real graph's /chatter should reach the merged graph")
        self.assertEqual(received[0].topic, "/scan")
        self.assertEqual(chatter[0].topic, "/chatter")

        # 3. A message published through the relay reaches the upstream it
        #    came from - and is not echoed back to the relay's own
        #    subscribers.
        delivered: list = []
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, delivered.append)
        self.assertTrue(_wait_for(lambda: len(delivered) == 0))

        resp = stub.Publish(pb.PublishRequest(
            topic="/cmd_vel", type=msg.TWIST,
            data=msg.encode({"linear": {"x": 0.5, "y": 0.0, "z": 0.0},
                             "angular": {"x": 0.0, "y": 0.0, "z": 0.0}}, msg.TWIST),
            encoding="cdr"))
        self.assertTrue(resp.accepted, resp.error)

        self.assertTrue(_wait_for(lambda: len(delivered) >= 1),
                        "a command published through the relay should reach the simulator")
        self.assertEqual(len(received), 1,
                         "a message published through the relay must not echo back")
        del sim_cmd

    def test_a_command_goes_to_the_side_that_consumes_it(self) -> None:
        """A downstream publish acts where the topic is *consumed*.

        The real graph publishes /cmd_vel (nav2's velocity smoother does) and
        the simulator consumes it (the car does). A command from downstream
        means "act on this", so it must reach the simulator - not the graph
        that happens to publish the topic. Routing it back to the publisher is
        the bug this covers: the car would never move.
        """
        sim_graph = Graph()
        real_graph = Graph()
        real_graph.create_publisher("/cmd_vel", msg.TWIST)
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, lambda event: None)

        stub, sim, real = self._relay(sim_graph, real_graph)
        # Let both upstreams be read at least once, so the relay knows which
        # side is the sink.
        self.assertTrue(_wait_for(lambda: "/cmd_vel" in sim.all_topics
                                           and "/cmd_vel" in real.all_topics))

        arrived: list = []
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, arrived.append)

        resp = stub.Publish(pb.PublishRequest(
            topic="/cmd_vel", type=msg.TWIST,
            data=msg.encode({"linear": {"x": 0.3, "y": 0.0, "z": 0.0},
                             "angular": {"x": 0.0, "y": 0.0, "z": 0.0}}, msg.TWIST),
            encoding="cdr"))
        self.assertTrue(resp.accepted, resp.error)
        self.assertTrue(_wait_for(lambda: len(arrived) >= 1),
                        "the simulator, which consumes /cmd_vel, should receive the command")

    def test_colliding_topics_are_kept_apart(self) -> None:
        """Both sides publish /scan: the loser gets a name of its own."""
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)
        real_graph.create_publisher("/scan", msg.LASER_SCAN)
        sim_graph.create_publisher("/only_sim", msg.STRING)

        stub, _sim, _real = self._relay(sim_graph, real_graph, collision="prefer-sim")

        self.assertTrue(
            _wait_for(lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
                      >= {"/scan", "/real/scan"}),
            "the colliding topic should appear under both names",
        )
        names = {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
        self.assertIn("/scan", names, "the winner keeps the plain name")
        self.assertIn("/real/scan", names, "the loser gets a prefixed name")
        self.assertIn("/only_sim", names, "a topic nobody collides with is untouched")

        # The two must not share a topic: the relay routes each source to its
        # own merged name.
        self.assertEqual(self.relay._route.get("/scan"), ("sim", "/scan"))
        self.assertEqual(self.relay._route.get("/real/scan"), ("real", "/scan"))

    def test_collision_policy_can_favour_the_real_graph(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)
        real_graph.create_publisher("/scan", msg.LASER_SCAN)

        stub, _sim, _real = self._relay(sim_graph, real_graph, collision="prefer-real")

        self.assertTrue(
            _wait_for(lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
                      >= {"/scan", "/sim/scan"}),
            "the simulator's colliding topic should be prefixed",
        )
        self.assertEqual(self.relay._route.get("/scan"), ("real", "/scan"))
        self.assertEqual(self.relay._route.get("/sim/scan"), ("sim", "/scan"))

    def test_services_are_merged_and_routed(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()
        calls: list = []

        sim_graph.create_service(
            "/sim/reset", "std_srvs/srv/Trigger",
            lambda request: {"success": True, "message": "sim"})

        # The real graph serves a different service. Its type is one the
        # simulator knows, so the request and the response can be encoded and
        # compared byte for byte.
        def real_set_bool(request):
            calls.append(request)
            return {"success": True, "message": "ok"}

        real_graph.create_service(
            "/real/set_bool", "std_srvs/srv/SetBool", real_set_bool)

        stub, _sim, _real = self._relay(sim_graph, real_graph)

        self.assertTrue(
            _wait_for(lambda: {s.name for s in stub.ListServices(pb.ListServicesRequest()).services}
                      >= {"/sim/reset", "/real/set_bool"}),
            "both sides' services should be advertised",
        )

        # A call is routed to the upstream that serves it.
        resp = stub.CallService(pb.CallServiceRequest(
            name="/real/set_bool", type="std_srvs/srv/SetBool",
            request=msg.encode({"data": True, "message": "hi"}, "std_srvs/srv/SetBool_Request"),
            encoding="cdr"))
        self.assertTrue(resp.success, resp.error)
        # The handler sees the decoded request. The `message` field is not
        # part of the request schema, so only `data` comes through.
        self.assertEqual(calls, [{"data": True}])

        # And the simulator's own service answers too.
        resp = stub.CallService(pb.CallServiceRequest(
            name="/sim/reset", type="std_srvs/srv/Trigger",
            request=msg.encode({}, "std_srvs/srv/Trigger_Request"), encoding="cdr"))
        self.assertTrue(resp.success, resp.error)

    def test_actions_are_merged_and_routed(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()

        sim_graph.create_action("/sim/spin", "nav2_msgs/action/Spin")
        real_graph.create_action("/real/spin", "nav2_msgs/action/Spin")

        stub, _sim, _real = self._relay(sim_graph, real_graph)

        self.assertTrue(
            _wait_for(lambda: {a.name for a in stub.ListActions(pb.ListActionsRequest()).actions}
                      >= {"/sim/spin", "/real/spin"}),
            "both sides' actions should be advertised",
        )

        # A goal goes to the upstream that owns the action. Neither graph has
        # a handler for Spin here, so it is refused - which proves the call
        # was routed to *an* upstream rather than swallowed.
        resp = stub.SendActionGoal(pb.SendActionGoalRequest(
            name="/sim/spin", type="nav2_msgs/action/Spin",
            goal=msg.encode({"target_yaw": 0.0, "time_allowance": {"sec": 5, "nanosec": 0}, "disable_collision_checks": False}, "nav2_msgs/action/Spin_Goal"), encoding="cdr"))
        self.assertFalse(resp.accepted, "the simulator serves no Spin goal here")

        resp = stub.SendActionGoal(pb.SendActionGoalRequest(
            name="/real/spin", type="nav2_msgs/action/Spin",
            goal=msg.encode({"target_yaw": 0.0, "time_allowance": {"sec": 5, "nanosec": 0}, "disable_collision_checks": False}, "nav2_msgs/action/Spin_Goal"), encoding="cdr"))
        self.assertFalse(resp.accepted, "the real graph serves no Spin goal here")

        # An action nobody serves is a NOT_FOUND, not a silent success.
        with self.assertRaises(grpc.RpcError) as caught:
            stub.SendActionGoal(pb.SendActionGoalRequest(
                name="/nobody", type="nav2_msgs/action/Spin",
                goal=msg.encode({"target_yaw": 0.0, "time_allowance": {"sec": 5, "nanosec": 0}, "disable_collision_checks": False}, "nav2_msgs/action/Spin_Goal"), encoding="cdr"))
        self.assertEqual(caught.exception.code(), grpc.StatusCode.NOT_FOUND)

    def test_action_goal_id_names_its_upstream(self) -> None:
        """The goal id has to say which upstream owns it."""
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_action("/sim/spin", "nav2_msgs/action/Spin")
        real_graph.create_action("/real/spin", "nav2_msgs/action/Spin")

        stub, _sim, _real = self._relay(sim_graph, real_graph)
        self.assertTrue(
            _wait_for(lambda: {a.name for a in stub.ListActions(pb.ListActionsRequest()).actions}
                      >= {"/sim/spin", "/real/spin"}),
            "both sides' actions should be advertised",
        )

        # A goal id minted by the relay names its upstream, so the relay can
        # resolve it back when the downstream follows, polls or cancels it.
        upstream, real_id = self.relay.split_goal_id("sim:abc123")
        self.assertEqual(upstream.name, "sim")
        self.assertEqual(real_id, "abc123")

        upstream, real_id = self.relay.split_goal_id("real:xyz")
        self.assertEqual(upstream.name, "real")
        self.assertEqual(real_id, "xyz")

    def test_a_goal_is_proxied_to_the_upstream_that_serves_it(self) -> None:
        """The whole action round trip, through the relay, to a real handler.

        This is the case the connector has to make work: a goal sent from
        downstream lands on the upstream that owns the action, the feedback
        comes back, and the goal ends with a result.
        """
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_action("/sim/spin", "nav2_msgs/action/Spin")
        real_graph.create_action("/real/spin", "nav2_msgs/action/Spin")

        stub, _sim, _real = self._relay(sim_graph, real_graph)

        seen: list = []

        def handler(goal, feedback, cancel):  # noqa: ANN001 - the agent's contract
            seen.append(goal)
            feedback({"angular_distance_traveled": 0.25})
            return ({"total_elapsed_time": {"sec": 1, "nanosec": 0},
                     "error_code": 0, "error_msg": ""}, "")

        # The upstream that owns the action gets a real implementation.
        self.real_agent.register_action("/real/spin", handler)

        self.assertTrue(
            _wait_for(lambda: {a.name for a in stub.ListActions(pb.ListActionsRequest()).actions}
                      >= {"/sim/spin", "/real/spin"}),
            "both sides' actions should be advertised",
        )

        goal = msg.encode({"target_yaw": 0.5,
                           "time_allowance": {"sec": 10, "nanosec": 0},
                           "disable_collision_checks": False},
                          "nav2_msgs/action/Spin_Goal")
        resp = stub.SendActionGoal(pb.SendActionGoalRequest(
            name="/real/spin", type="nav2_msgs/action/Spin", goal=goal, encoding="cdr"))
        self.assertTrue(resp.accepted, resp.error)
        self.assertTrue(resp.goal_id.startswith("real:"),
                        "the goal id must name the upstream that owns it")

        # The feedback follows the goal, and the goal ends in a result.
        status = None
        for message in stub.ActionFeedback(pb.ActionFeedbackRequest(
                name="/real/spin", goal_id=resp.goal_id)):
            status = message.status
            if status in (pb.GOAL_STATUS_SUCCEEDED, pb.GOAL_STATUS_ABORTED,
                          pb.GOAL_STATUS_CANCELED):
                break
        self.assertEqual(status, pb.GOAL_STATUS_SUCCEEDED)
        self.assertEqual(len(seen), 1, "the handler on the upstream should have run")
        self.assertEqual(seen[0]["target_yaw"], 0.5)

        result = stub.GetActionResult(pb.GetActionResultRequest(
            name="/real/spin", goal_id=resp.goal_id, timeout_ms=5000))
        self.assertTrue(result.success, result.error)
        decoded = msg.decode(result.result, "nav2_msgs/action/Spin_Result")
        self.assertEqual(decoded["error_code"], 0)


    def test_colliding_topics_keep_their_data_apart(self) -> None:
        """The names are split *and* the bytes are split.

        The earlier version registered ``/real/scan`` in the graph but still
        forwarded the real robot's messages under the plain name, so both
        streams landed on ``/scan``. Listing the topic is not enough; the
        message itself has to carry its own marker.
        """
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)
        real_graph.create_publisher("/scan", msg.LASER_SCAN)

        stub, sim, real = self._relay(sim_graph, real_graph, collision="prefer-sim")
        self.assertTrue(
            _wait_for(lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
                      >= {"/scan", "/real/scan"}),
            "the collision should be split into two names",
        )
        self.assertEqual(self.relay._route.get("/scan"), ("sim", "/scan"))
        self.assertEqual(self.relay._route.get("/real/scan"), ("real", "/scan"))

        sim_seen: list = []
        real_seen: list = []
        for name, sink in (("/scan", sim_seen), ("/real/scan", real_seen)):
            stream = stub.Subscribe(pb.SubscribeRequest(topic=name, type=msg.LASER_SCAN))
            threading.Thread(target=_collect, args=(stream, sink), daemon=True).start()

        self.assertTrue(
            _wait_for(lambda: all(
                (self.relay.graph.get_topic(name) or
                 type("T", (), {"subscriber_count": 0})).subscriber_count >= 1
                for name in ("/scan", "/real/scan"))),
            "both merged topics should have a downstream subscriber",
        )
        self.assertTrue(_wait_for(lambda: "/scan" in sim.subs and "/scan" in real.subs),
                        "the relay should stream each upstream's /scan")

        # A distinct marker per source makes a mix impossible to miss. Keep
        # publishing until both sides have arrived: on a loaded CI runner the
        # upstream stream may attach after the first publish.
        deadline = time.monotonic() + TIMEOUT
        index = 0
        while time.monotonic() < deadline and (len(sim_seen) < 5 or len(real_seen) < 5):
            sim_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(1.5 + index * 0.01))
            real_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(9.5 + index * 0.01))
            index += 1
            time.sleep(0.05)

        self.assertGreaterEqual(len(sim_seen), 5,
                                "the simulator's /scan should keep flowing")
        self.assertGreaterEqual(len(real_seen), 5,
                                "the real robot's /scan should keep flowing")
        for message in sim_seen:
            self.assertLess(_scan_marker(message), 2.0,
                            "the simulator's marker arrived on the real topic")
        for message in real_seen:
            self.assertGreater(_scan_marker(message), 9.0,
                               "the real robot's marker arrived on the simulator topic")

    def test_echo_suppression_does_not_swallow_a_new_message(self) -> None:
        """Only the relay's own copy is dropped, never the next real sample.

        The old rule muted the whole topic for 0.5 s after an injection, so a
        legitimate message that happened to arrive in that window was lost.
        The relay now drops the exact message it sent (the agent echoes its
        ``origin_id``) and lets everything else through.
        """
        sim_graph = Graph()
        real_graph = Graph()
        real_graph.create_publisher("/scan", msg.LASER_SCAN)

        stub, _sim, _real = self._relay(sim_graph, real_graph)
        received: list = []
        stream = stub.Subscribe(pb.SubscribeRequest(topic="/scan", type=msg.LASER_SCAN))
        threading.Thread(target=_collect, args=(stream, received), daemon=True).start()
        self.assertTrue(_wait_for(lambda: "/scan" in self.relay.real.subs))
        self.assertTrue(
            _wait_for(lambda: (self.relay.graph.get_topic("/scan") or
                               type("T", (), {"subscriber_count": 0})).subscriber_count >= 1),
            "the downstream subscriber should be attached",
        )

        # Warm up: proves the subscription really is established upstream.
        real_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(3.0))
        self.assertTrue(_wait_for(lambda: len(received) >= 1))
        received.clear()

        resp = stub.Publish(pb.PublishRequest(
            topic="/scan", type=msg.LASER_SCAN, data=_scan_payload(5.0), encoding="cdr"))
        self.assertTrue(resp.accepted, resp.error)
        self.assertEqual(resp.result, "ACCEPTED")

        # This one is *not* an echo and must survive, even though it lands
        # well inside the old 0.5 s mute window.
        real_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(7.0))

        self.assertTrue(_wait_for(lambda: len(received) >= 1),
                        "the legitimate message was swallowed by echo suppression")
        markers = [_scan_marker(message) for message in received]
        self.assertTrue(all(marker > 6.0 for marker in markers),
                        "the injected message echoed downstream: %r" % markers)

        # The counters are the first piece of the observability work: they
        # must distinguish data that crossed from data that was an echo.
        self.assertTrue(_wait_for(
            lambda: self.relay.stats_snapshot()["global"]["dropped_echo"] >= 1),
            "the suppressed echo should be counted")
        snapshot = self.relay.stats_snapshot()
        self.assertGreaterEqual(snapshot["global"]["merged"], 1)
        self.assertGreaterEqual(snapshot["topics"]["/scan"]["received"], 1)

    def test_a_dead_subscription_is_recreated(self) -> None:
        """A stream that ends must not leave a dead dict entry behind."""
        sim_graph = Graph()
        real_graph = Graph()
        real_graph.create_publisher("/chatter", msg.STRING)

        stub, _sim, _real = self._relay(sim_graph, real_graph)
        chatter: list = []
        stream = stub.Subscribe(pb.SubscribeRequest(topic="/chatter", type=msg.STRING))
        threading.Thread(target=_collect, args=(stream, chatter), daemon=True).start()

        self.assertTrue(_wait_for(lambda: "/chatter" in self.relay.real.subs))
        sub = self.relay.real.subs["/chatter"]
        self.assertTrue(_wait_for(lambda: sub.state == sub.ACTIVE),
                        "the stream should be active before it is broken")
        old_thread = sub.thread

        # Break the stream the way a network drop does: cancel the RPC.
        sub.call.cancel()
        self.assertTrue(_wait_for(lambda: sub.state in (sub.RETRYING, sub.FAILED)),
                        "the subscription should notice the stream ended")
        self.assertTrue(
            _wait_for(lambda: sub.thread is not old_thread and sub.alive, timeout=10.0),
            "the pump should start a new stream after the backoff",
        )

        for _ in range(80):
            real_graph.publish("/chatter", msg.STRING,
                               msg.encode({"data": "after-restart"}, msg.STRING))
            if chatter:
                break
            time.sleep(0.1)
        self.assertTrue(chatter, "no message after the subscription was recreated")
        self.assertEqual(msg.decode(chatter[0].data, msg.STRING)["data"], "after-restart")
        self.assertGreaterEqual(
            self.relay.stats_snapshot()["global"]["reconnects"], 1,
            "a stream rebuild should be counted")

    def test_publish_to_an_unreachable_upstream_is_not_reported_as_accepted(self) -> None:
        """A failed send has to say so; ``accepted=True`` is a lie."""
        sim_graph = Graph()
        sim_delivered: list = []
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, sim_delivered.append)

        sim_agent, sim_port = self._agent(sim_graph)
        del sim_agent
        sim = _Upstream("sim", "127.0.0.1:%d" % sim_port)
        # Reserve the port with a bound socket that never listens, so no other
        # test's server can take it between `_free_port()` and the probe.
        dead = socket.socket()
        dead.bind(("127.0.0.1", 0))
        self.addCleanup(dead.close)
        real = _Upstream("real", "127.0.0.1:%d" % dead.getsockname()[1])
        self.relay = Relay(sim, real, refresh_s=0.2,
                           control_routes={"/cmd_vel": "real"})
        self.relay.start()
        relay_port = _free_port()
        self.relay.serve("127.0.0.1", relay_port, "", 0)
        channel = grpc.insecure_channel("127.0.0.1:%d" % relay_port)
        self.addCleanup(channel.close)
        stub = pb_grpc.Ros2BridgeStub(channel)

        resp = stub.Publish(pb.PublishRequest(
            topic="/cmd_vel", type=msg.TWIST, data=_twist(0.4), encoding="cdr"))
        self.assertFalse(resp.accepted, "a command that never left must not be accepted")
        self.assertEqual(resp.result, "UNAVAILABLE", resp.error)
        # The pump may be between reconnect attempts, so the message is either
        # "upstream real is not reachable" or the channel's own error. Either
        # way it must be reported, not swallowed.
        self.assertTrue(resp.error, "a failed publish must carry a reason")
        self.assertEqual(sim_delivered, [],
                         "a control command must not fall back to the simulator")

    def test_an_ambiguous_control_route_is_refused(self) -> None:
        """Both sides consume /cmd_vel: refuse rather than pick one."""
        sim_graph = Graph()
        real_graph = Graph()
        sim_delivered: list = []
        real_delivered: list = []
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, sim_delivered.append)
        real_graph.create_subscription("/cmd_vel", msg.TWIST, real_delivered.append)

        stub, sim, real = self._relay(sim_graph, real_graph)
        self.assertTrue(_wait_for(lambda: "/cmd_vel" in sim.all_topics
                                           and "/cmd_vel" in real.all_topics))

        resp = stub.Publish(pb.PublishRequest(
            topic="/cmd_vel", type=msg.TWIST, data=_twist(0.4), encoding="cdr"))
        self.assertFalse(resp.accepted, "an ambiguous control route must be refused")
        self.assertEqual(resp.result, "REJECTED", resp.error)
        self.assertIn("ambiguous", resp.error)
        self.assertEqual(sim_delivered, [])
        self.assertEqual(real_delivered, [])

    def test_an_explicit_control_route_wins_and_is_clamped(self) -> None:
        """The configured destination is used even when both consume it."""
        sim_graph = Graph()
        real_graph = Graph()
        sim_delivered: list = []
        real_delivered: list = []
        sim_graph.create_subscription("/cmd_vel", msg.TWIST, sim_delivered.append)
        real_graph.create_subscription("/cmd_vel", msg.TWIST, real_delivered.append)

        stub, sim, real = self._relay(
            sim_graph, real_graph, control_routes={"/cmd_vel": "real"},
            max_linear_speed=0.5)
        self.assertTrue(_wait_for(lambda: "/cmd_vel" in sim.all_topics
                                           and "/cmd_vel" in real.all_topics))

        resp = stub.Publish(pb.PublishRequest(
            topic="/cmd_vel", type=msg.TWIST, data=_twist(2.0), encoding="cdr"))
        self.assertTrue(resp.accepted, resp.error)
        self.assertEqual(resp.result, "ACCEPTED")
        self.assertTrue(_wait_for(lambda: len(real_delivered) >= 1))
        self.assertEqual(sim_delivered, [], "the explicit route must be respected")
        decoded = msg.decode(real_delivered[0].data, msg.TWIST)
        self.assertAlmostEqual(decoded["linear"]["x"], 0.5, places=5,
                               msg="the configured speed clamp was not applied")


class RelayLifecycleTest(unittest.TestCase):
    """Cases that need a relay object but no running agents."""

    def _bare_relay(self) -> Relay:
        sim = _Upstream("sim", "127.0.0.1:1")
        real = _Upstream("real", "127.0.0.1:2")
        return Relay(sim, real, refresh_s=0.05)

    def test_backoff_grows_when_a_stream_fails_immediately(self) -> None:
        """The failure counter must not be reset just because an RPC object
        exists: a stream that dies at once has to climb the ladder."""
        relay = self._bare_relay()

        class _Failed(grpc.RpcError):
            def code(self):  # noqa: ANN001
                return grpc.StatusCode.UNAVAILABLE

        class _FailingStub:
            def Subscribe(self, request):  # noqa: ANN001
                def events():
                    raise _Failed()
                    yield  # pragma: no cover - makes this a generator

                return events()

        relay.real.stub = _FailingStub()
        sub = _Subscription(relay.real, "/scan", msg.LASER_SCAN)

        delays = []
        for _ in range(10):
            relay._stream_topic(sub)
            delays.append(sub.retry_at - time.monotonic())

        self.assertEqual(sub.attempt, 10, "each fast failure is one attempt")
        self.assertGreater(delays[-1], delays[0] * 2,
                           "the backoff must grow, not restart at the base")
        self.assertLessEqual(delays[-1], SUB_BACKOFF_MAX_S * 1.5 + 1.0,
                             "the backoff must stay bounded")
        self.assertGreaterEqual(relay.stats_snapshot()["global"]["reconnects"], 10)

    def test_a_foreign_origin_id_is_not_deduplicated_by_payload(self) -> None:
        """Only the fingerprint fallback may compare bytes, and only for an
        agent that cannot tag provenance at all."""
        relay = self._bare_relay()
        payload = _scan_payload(4.2)
        relay._remember_injection("real", "/scan", payload, "relay-ours:1")

        class _Message:
            def __init__(self, origin_id, source_id):
                self.origin_id = origin_id
                self.source_id = source_id
                self.data = payload

        # Another relay's message: a foreign id is data, never our echo.
        self.assertFalse(relay._is_echo(
            relay.real, "/scan", _Message("other-relay:7", "agent-x")))
        # A new agent with no origin id proves it can tag and this is not ours.
        relay._remember_injection("real", "/scan", payload, "relay-ours:2")
        self.assertFalse(relay._is_echo(
            relay.real, "/scan", _Message("", "agent-y")))
        # An old agent has neither field, so the byte fallback still works.
        relay._remember_injection("real", "/scan", payload, "relay-ours:3")
        self.assertTrue(relay._is_echo(
            relay.real, "/scan", _Message("", "")))


class RelayGraphSnapshotTest(unittest.TestCase):
    """The graph a downstream sees is a snapshot, not an append-only log."""

    def setUp(self) -> None:
        self.servers = []
        self.relay = None

    def tearDown(self) -> None:
        if self.relay is not None:
            self.relay.stop()
        for server, _agent, _port in self.servers:
            stop_server(server)

    def _agent(self, graph: Graph):
        server, agent, port = build_server(graph, "127.0.0.1", 0)
        server.start()
        self.servers.append((server, agent, port))
        return agent, port

    def _relay(self, sim_graph: Graph, real_graph: Graph):
        _sim_agent, sim_port = self._agent(sim_graph)
        _real_agent, real_port = self._agent(real_graph)
        sim = _Upstream("sim", "127.0.0.1:%d" % sim_port)
        real = _Upstream("real", "127.0.0.1:%d" % real_port)
        self.relay = Relay(sim, real, refresh_s=0.2)
        self.relay.start()
        relay_port = _free_port()
        self.relay.serve("127.0.0.1", relay_port, "", 0)
        channel = grpc.insecure_channel("127.0.0.1:%d" % relay_port)
        self.addCleanup(channel.close)
        return pb_grpc.Ros2BridgeStub(channel)

    def test_removed_topics_services_and_actions_disappear(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()
        publisher = real_graph.create_publisher("/gone_topic", msg.STRING)
        real_graph.create_service(
            "/gone_srv", "std_srvs/srv/Trigger",
            lambda request: {"success": True, "message": "ok"})
        real_graph.create_action("/gone_act", "nav2_msgs/action/Spin")

        stub = self._relay(sim_graph, real_graph)
        self.assertTrue(_wait_for(
            lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
            >= {"/gone_topic"}))
        self.assertTrue(_wait_for(
            lambda: {s.name for s in stub.ListServices(pb.ListServicesRequest()).services}
            >= {"/gone_srv"}))
        self.assertTrue(_wait_for(
            lambda: {a.name for a in stub.ListActions(pb.ListActionsRequest()).actions}
            >= {"/gone_act"}))

        # A downstream subscriber keeps the graph *topic* alive; the list must
        # still stop advertising it once the source is gone.
        stream = stub.Subscribe(pb.SubscribeRequest(topic="/gone_topic", type=msg.STRING))
        threading.Thread(target=_collect, args=(stream, []), daemon=True).start()
        self.assertTrue(_wait_for(
            lambda: (self.relay.graph.get_topic("/gone_topic") or
                     type("T", (), {"subscriber_count": 0})).subscriber_count >= 1))

        publisher.unadvertise()
        real_graph.remove_service("/gone_srv")
        real_graph.remove_action("/gone_act")

        self.assertTrue(_wait_for(
            lambda: "/gone_topic" not in
            {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}),
            "a topic whose publisher exited must leave the merged list")
        self.assertTrue(_wait_for(
            lambda: "/gone_srv" not in
            {s.name for s in stub.ListServices(pb.ListServicesRequest()).services}),
            "a deleted service must leave the merged list")
        self.assertTrue(_wait_for(
            lambda: "/gone_act" not in
            {a.name for a in stub.ListActions(pb.ListActionsRequest()).actions}),
            "a deleted action must leave the merged list")

    def test_a_recreated_topic_can_have_a_new_type(self) -> None:
        sim_graph = Graph()
        real_graph = Graph()
        publisher = real_graph.create_publisher("/reborn", msg.STRING)
        stub = self._relay(sim_graph, real_graph)
        self.assertTrue(_wait_for(
            lambda: {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}
            >= {"/reborn"}))

        publisher.unadvertise()
        self.assertTrue(_wait_for(
            lambda: "/reborn" not in
            {t.name for t in stub.ListTopics(pb.ListTopicsRequest()).topics}))
        # The relay cancels its stream once the publisher is gone; the agent
        # then has no endpoint left and drops the topic with its old type.
        self.assertTrue(_wait_for(lambda: real_graph.get_topic("/reborn") is None),
                        "the agent should release the topic before it is recreated")
        real_graph.create_publisher("/reborn", msg.LASER_SCAN)
        self.assertTrue(_wait_for(
            lambda: any(t.name == "/reborn" and t.type == msg.LASER_SCAN
                        for t in stub.ListTopics(pb.ListTopicsRequest()).topics)),
            "the recreated topic must be advertised with its new type")

    def test_the_relay_reports_the_latency_of_what_it_merged(self) -> None:
        """One-way delay per upstream and per topic, plus the stale stamps.

        The relay is where a link's latency becomes visible: the agent
        stamps every message with the wall clock at publish time, and the
        relay knows when it delivered it. Without that, "the robot feels
        laggy" has no number attached to it.
        """
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)
        real_graph.create_publisher("/chatter", msg.STRING)

        stub = self._relay(sim_graph, real_graph)
        # Wait for the relay's *own* stream to be open: the topic exists in
        # the graph as soon as the publisher does, and a message published
        # before the stream is open goes into a topic nobody was listening
        # to yet.
        self.assertTrue(
            _wait_for(lambda: self.relay.sim.subs.get("/scan") is not None
                      and self.relay.sim.subs["/scan"].state == "ACTIVE"),
            "the relay should have subscribed to the simulator")

        # A stamp 40 ms in the past: the sample should show up in the
        # percentiles, roughly that old.
        sim_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(),
                          stamp_ns=time.time_ns() - 40 * 10**6)
        self.assertTrue(
            _wait_for(lambda: self.relay.stats_snapshot()["latency"]["global"]["samples"] >= 1),
            "the relay should have recorded a latency sample")

        snapshot = self.relay.stats_snapshot()
        latency = snapshot["latency"]
        self.assertGreaterEqual(latency["global"]["samples"], 1)
        self.assertGreaterEqual(latency["global"]["p50_ms"], 30.0)
        self.assertLess(latency["global"]["p50_ms"], 5000.0)
        self.assertIn("p95_ms", latency["global"])
        self.assertIn("p99_ms", latency["global"])
        # Per upstream and per topic: the two machines' clocks are the only
        # thing that can lie about the absolute number.
        self.assertGreaterEqual(latency["upstreams"]["sim"]["samples"], 1)
        self.assertGreaterEqual(latency["topics"]["/scan"]["samples"], 1)
        self.assertGreaterEqual(snapshot["topics"]["/scan"]["merged"], 1)

        # A stamp in the future (clock skew) is not a latency: it is counted
        # as unusable instead of poisoning the percentiles.
        sim_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(),
                          stamp_ns=time.time_ns() + 60 * 10**9)
        self.assertTrue(
            _wait_for(lambda: self.relay.stats_snapshot()["latency"]["global"]
                      ["stale_stamps"] >= 1),
            "a stamp from the future should be counted as unusable")
        self.assertLessEqual(self.relay.stats_snapshot()["latency"]["global"]["p50_ms"], 5000.0)

    def test_a_message_the_merged_graph_refuses_is_counted(self) -> None:
        """A merge that fails is a dropped message, and it has to show up.

        The relay's counters are the only place where "the graph is full"
        or "the type changed" becomes visible; before, the exception was
        logged and the message was gone.
        """
        sim_graph = Graph()
        real_graph = Graph()
        sim_graph.create_publisher("/scan", msg.LASER_SCAN)

        stub = self._relay(sim_graph, real_graph)
        # Wait for the relay's *own* stream to be open: the topic exists in
        # the graph as soon as the publisher does, and a message published
        # before the stream is open goes into a topic nobody was listening
        # to yet.
        self.assertTrue(
            _wait_for(lambda: self.relay.sim.subs.get("/scan") is not None
                      and self.relay.sim.subs["/scan"].state == "ACTIVE"),
            "the relay should have subscribed to the simulator")

        def refuse(*_args, **_kwargs):
            raise RuntimeError("the graph says no")

        self.relay.graph.publish = refuse
        sim_graph.publish("/scan", msg.LASER_SCAN, _scan_payload(),
                          stamp_ns=time.time_ns())
        self.assertTrue(
            _wait_for(lambda: self.relay.stats_snapshot()["global"]["merge_errors"] >= 1),
            "a refused merge should be counted")
        snapshot = self.relay.stats_snapshot()
        self.assertGreaterEqual(snapshot["topics"]["/scan"]["merge_errors"], 1)


if __name__ == "__main__":
    unittest.main()
