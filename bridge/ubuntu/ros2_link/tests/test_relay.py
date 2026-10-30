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
from ros2_link.relay import Relay, _Upstream  # noqa: E402

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


def _scan_payload() -> bytes:
    return msg.encode(
        {"header": {"stamp": {"sec": 1, "nanosec": 0}, "frame_id": "laser"},
         "angle_min": 0.0, "angle_max": 1.0, "angle_increment": 0.1,
         "time_increment": 0.0, "scan_time": 0.05, "range_min": 0.1,
         "range_max": 12.0, "ranges": [1.0, 2.0, 3.0], "intensities": []},
        msg.LASER_SCAN)


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

    def _relay(self, sim_graph: Graph, real_graph: Graph, collision: str = "prefer-sim"):
        sim_agent, sim_port = self._agent(sim_graph)
        real_agent, real_port = self._agent(real_graph)
        self.sim_agent, self.real_agent = sim_agent, real_agent
        sim = _Upstream("sim", "127.0.0.1:%d" % sim_port)
        real = _Upstream("real", "127.0.0.1:%d" % real_port)
        self.relay = Relay(sim, real, refresh_s=0.2, collision=collision)
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


if __name__ == "__main__":
    unittest.main()
