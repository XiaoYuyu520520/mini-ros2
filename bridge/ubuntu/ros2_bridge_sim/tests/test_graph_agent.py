#!/usr/bin/env python3
"""End-to-end test: a real gRPC client against the bridge agent over a channel.

Starts the simulated car + agent on an ephemeral port, then exercises
Ping / ListTopics / GetTopicInfo / Subscribe / Publish with the real
``grpc`` client.

Run under pytest or standalone::

    python tests/test_graph_agent.py
"""

from __future__ import annotations

import os
import sys
import time
from contextlib import contextmanager
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import grpc  # noqa: E402

from ros2_bridge_sim import messages as m  # noqa: E402
from ros2_bridge_sim.agent import build_server, stop_server  # noqa: E402
from ros2_bridge_sim.graph import Graph  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402
from ros2_bridge_sim.sim import SIM_TOPICS, SimCar  # noqa: E402
from ros2_bridge_sim.world import World  # noqa: E402

HOST = "127.0.0.1"


def wait_for(predicate, timeout: float = 5.0, interval: float = 0.02) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return bool(predicate())


@contextmanager
def sim_stack(*, rate: float = 100.0, seed: int = 1):
    """A running car + agent on an ephemeral port, with a connected client."""
    graph = Graph()
    world = World()
    world.populate(seed=seed)
    car = SimCar(graph, world, rate=rate)
    server, agent, port = build_server(graph, HOST, 0)  # port 0 -> ephemeral
    server.start()
    car.start()
    channel = grpc.insecure_channel("%s:%d" % (HOST, port))
    grpc.channel_ready_future(channel).result(timeout=5.0)
    stub = pb_grpc.Ros2BridgeStub(channel)
    try:
        yield SimpleNamespace(
            graph=graph, world=world, car=car, agent=agent, port=port, channel=channel, stub=stub
        )
    finally:
        channel.close()
        car.stop()
        stop_server(server, grace=0.5)


def topic_names(stub) -> dict[str, str]:
    response = stub.ListTopics(pb.ListTopicsRequest())
    return {topic.name: topic.type for topic in response.topics}


# --------------------------------------------------------------------------
# Ping / discovery
# --------------------------------------------------------------------------

def test_ping_reports_version_and_sim_distro():
    with sim_stack() as stack:
        response = stack.stub.Ping(pb.PingRequest(payload="hello bridge"))
        assert response.payload == "hello bridge"
        assert response.agent_version == "ros2_bridge_sim/0.1"
        assert response.ros_distro == "sim"


def test_list_topics_shows_the_simulated_topics():
    with sim_stack() as stack:
        topics = topic_names(stack.stub)
        for name in ("/scan", "/odom", "/map", "/tf"):
            assert name in topics, "%s missing from %s" % (name, sorted(topics))
        for name, type_name in SIM_TOPICS:
            assert topics.get(name) == type_name, "%s has type %r" % (name, topics.get(name))
        assert topics["/scan"] == "sensor_msgs/msg/LaserScan"
        assert topics["/odom"] == "nav_msgs/msg/Odometry"
        assert topics["/map"] == "nav_msgs/msg/OccupancyGrid"
        assert topics["/tf"] == "tf2_msgs/msg/TFMessage"


def test_list_topics_honours_the_name_filter_regex():
    with sim_stack() as stack:
        response = stack.stub.ListTopics(pb.ListTopicsRequest(name_filter="^/(scan|odom)$"))
        assert sorted(topic.name for topic in response.topics) == ["/odom", "/scan"]

        response = stack.stub.ListTopics(pb.ListTopicsRequest(name_filter="tf"))
        assert sorted(topic.name for topic in response.topics) == ["/tf", "/tf_static"]

        response = stack.stub.ListTopics(pb.ListTopicsRequest(name_filter="nope"))
        assert list(response.topics) == []


def test_get_topic_info():
    with sim_stack() as stack:
        info = stack.stub.GetTopicInfo(pb.GetTopicInfoRequest(name="/scan"))
        assert info.name == "/scan"
        assert info.type == "sensor_msgs/msg/LaserScan"
        assert info.publisher_count == 1
        assert info.subscriber_count == 0

        info = stack.stub.GetTopicInfo(pb.GetTopicInfoRequest(name="/cmd_vel"))
        assert info.type == "geometry_msgs/msg/Twist"
        assert info.subscriber_count == 1  # the car is listening

        with_unknown = None
        try:
            stack.stub.GetTopicInfo(pb.GetTopicInfoRequest(name="/does_not_exist"))
        except grpc.RpcError as exc:
            with_unknown = exc.code()
        assert with_unknown == grpc.StatusCode.NOT_FOUND


# --------------------------------------------------------------------------
# Subscribe
# --------------------------------------------------------------------------

def test_subscribe_to_scan_receives_cdr_messages():
    with sim_stack() as stack:
        call = stack.stub.Subscribe(pb.SubscribeRequest(topic="/scan", type="sensor_msgs/msg/LaserScan"))
        received = []
        deadline = time.monotonic() + 5.0
        while len(received) < 3 and time.monotonic() < deadline:
            try:
                received.append(next(call))
            except StopIteration:
                break
        call.cancel()

        assert len(received) >= 3, "got %d messages in 5 s" % len(received)
        for message in received:
            assert message.topic == "/scan"
            assert message.type == "sensor_msgs/msg/LaserScan"
            assert message.encoding == "cdr"
            assert message.stamp_ns > 0
            scan = m.decode(message.data, message.type)
            assert len(scan["ranges"]) == 360
            assert scan["header"]["frame_id"] == "laser"
        sequences = [message.sequence for message in received]
        assert sequences == sorted(sequences) and sequences[0] < sequences[-1]


def test_subscribe_without_a_type_resolves_it_from_the_graph():
    with sim_stack() as stack:
        call = stack.stub.Subscribe(pb.SubscribeRequest(topic="/odom"))
        message = next(call)
        call.cancel()
        assert message.type == "nav_msgs/msg/Odometry"
        assert message.encoding == "cdr"
        assert m.decode(message.data, message.type)["child_frame_id"] == "base_link"


def test_multiple_concurrent_subscribers_each_get_every_message():
    with sim_stack() as stack:
        calls = [stack.stub.Subscribe(pb.SubscribeRequest(topic="/scan")) for _ in range(3)]
        try:
            first = [next(call) for call in calls]
            for message in first:
                assert message.topic == "/scan"
                assert len(message.data) > 0
            # each subscriber saw the same message sequence
            assert len({message.sequence for message in first}) == 1
        finally:
            for call in calls:
                call.cancel()
        assert wait_for(lambda: stack.graph.get_topic("/scan").subscriber_count == 0, 2.0)


def test_cancelling_a_subscription_cleans_up():
    with sim_stack() as stack:
        call = stack.stub.Subscribe(pb.SubscribeRequest(topic="/scan"))
        next(call)
        assert wait_for(lambda: stack.graph.get_topic("/scan").subscriber_count == 1, 2.0)
        call.cancel()
        assert wait_for(lambda: stack.graph.get_topic("/scan").subscriber_count == 0, 2.0)


# --------------------------------------------------------------------------
# Publish
# --------------------------------------------------------------------------

def test_publish_cmd_vel_drives_the_car():
    with sim_stack() as stack:
        assert stack.car.cmd == (0.0, 0.0)
        pose_before = stack.car.pose

        payload = m.encode(m.make_twist(0.5, -0.3), m.TWIST)
        response = stack.stub.Publish(
            pb.PublishRequest(topic="/cmd_vel", type=m.TWIST, data=payload, encoding="cdr")
        )
        assert response.accepted, response.error

        assert wait_for(lambda: stack.car.cmd == (0.5, -0.3), 2.0), stack.car.cmd
        assert wait_for(lambda: stack.car.pose[0] > pose_before[0] + 0.05, 2.0), stack.car.pose
        assert stack.car.pose[2] < 0.0  # turning right

        # publish an empty encoding too: it means cdr
        response = stack.stub.Publish(
            pb.PublishRequest(topic="/cmd_vel", type=m.TWIST, data=m.encode(m.make_twist(0.0, 0.0), m.TWIST))
        )
        assert response.accepted, response.error
        assert wait_for(lambda: stack.car.cmd == (0.0, 0.0), 2.0)


def test_publish_rejects_unsupported_encoding_and_survives():
    with sim_stack() as stack:
        response = stack.stub.Publish(
            pb.PublishRequest(topic="/cmd_vel", type=m.TWIST, data=b"{}", encoding="json")
        )
        assert response.accepted is False
        assert "json" in response.error
        assert stack.car.cmd == (0.0, 0.0)

        # the server is still alive
        assert stack.stub.Ping(pb.PingRequest(payload="still here")).payload == "still here"


def test_publish_rejects_undecodable_payloads_and_survives():
    with sim_stack() as stack:
        for payload, label in (
            (b"\x00\x01\x00\x00\xff\xff", "truncated body"),
            (b"\x00\x01\x00\x00", "header only"),
            (b"\x02\x03\x04\x05" + b"\x00" * 8, "bad encapsulation header"),
            (m.encode(m.make_twist(1.0, 0.0), m.TWIST)[:12], "cut in half"),
        ):
            response = stack.stub.Publish(
                pb.PublishRequest(topic="/cmd_vel", type=m.TWIST, data=payload, encoding="cdr")
            )
            assert response.accepted is False, label
            assert response.error, label

        assert stack.car.cmd == (0.0, 0.0)
        assert stack.stub.Ping(pb.PingRequest(payload="ok")).payload == "ok"


def test_publish_to_an_unknown_type_is_forwarded():
    """A type we have no schema for is passed through instead of rejected."""
    with sim_stack() as stack:
        payload = m.encode({"data": "hi"}, "std_msgs/msg/String")
        response = stack.stub.Publish(
            pb.PublishRequest(topic="/custom_topic", type="std_msgs/msg/String", data=payload)
        )
        assert response.accepted, response.error

        received = []
        call = stack.stub.Subscribe(
            pb.SubscribeRequest(topic="/custom_topic", type="std_msgs/msg/String")
        )
        try:
            response = stack.stub.Publish(
                pb.PublishRequest(topic="/custom_topic", type="std_msgs/msg/String", data=payload)
            )
            assert response.accepted
            received.append(next(call))
        finally:
            call.cancel()
        assert m.decode(received[0].data, "std_msgs/msg/String")["data"] == "hi"


def test_list_topics_is_not_disturbed_by_a_subscriber_quitting():
    with sim_stack() as stack:
        for _ in range(3):
            call = stack.stub.Subscribe(pb.SubscribeRequest(topic="/scan"))
            next(call)
            call.cancel()
        assert wait_for(lambda: stack.graph.get_topic("/scan").subscriber_count == 0, 2.0)
        assert stack.stub.Ping(pb.PingRequest(payload="alive")).payload == "alive"


def main() -> int:
    failures = 0
    for name in sorted(globals()):
        if not name.startswith("test_"):
            continue
        function = globals()[name]
        started = time.monotonic()
        try:
            function()
        except AssertionError as exc:
            failures += 1
            print("FAIL %s: %s" % (name, exc))
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print("ERROR %s: %s: %s" % (name, type(exc).__name__, exc))
        else:
            print("PASS %s (%.2fs)" % (name, time.monotonic() - started))
    print("=" * 60)
    print("%d failed" % failures if failures else "all graph/agent end-to-end tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())