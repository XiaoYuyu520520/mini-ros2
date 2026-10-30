#!/usr/bin/env python3
"""End-to-end test: services and actions across the bridge agent.

The bridge carried topics only for a long time, which meant a caller on the
other side of the wire could watch the robot but not ask it anything or tell
it to do something. This exercises the two RPC families that closed that gap
with a real ``grpc`` client against a real agent:

* ``ListServices`` / ``GetServiceInfo`` / ``CallService``, and
* ``ListActions`` / ``SendActionGoal`` / ``ActionFeedback`` /
  ``GetActionResult`` / ``CancelActionGoal``.

Run under pytest or standalone::

    python tests/test_services.py
"""

from __future__ import annotations

import math
import os
import sys
import threading
import time
from contextlib import contextmanager
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import grpc  # noqa: E402
import pytest  # noqa: E402

from ros2_bridge_sim import messages as m  # noqa: E402
from ros2_bridge_sim import services  # noqa: E402
from ros2_bridge_sim.agent import build_server, stop_server  # noqa: E402
from ros2_bridge_sim.graph import Graph  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402
from ros2_bridge_sim.sim import SimCar  # noqa: E402
from ros2_bridge_sim.world import World  # noqa: E402

HOST = "127.0.0.1"

#: What a field-less CDR request looks like on the wire: the 4 byte
#: encapsulation header plus a body padded out to 4 zero bytes. Hand written
#: rather than taken from `messages.encode` so a change to the encoder cannot
#: quietly move both sides of the test at once, and so the test fails if the
#: encoder ever stops producing the form the DDS wire uses.
PADDED_EMPTY_REQUEST = b"\x00\x01\x00\x00\x00\x00\x00\x00"

TERMINAL = (
    pb.GOAL_STATUS_SUCCEEDED,
    pb.GOAL_STATUS_CANCELED,
    pb.GOAL_STATUS_ABORTED,
    pb.GOAL_STATUS_REJECTED,
)


@contextmanager
def bridge_stack(*, rate: float = 50.0, seed: int = 1, result_ttl_s: float = 60.0):
    """A running car + agent (with the sim services) on an ephemeral port."""
    graph = Graph()
    world = World()
    world.populate(seed=seed)
    car = SimCar(graph, world, rate=rate)
    server, agent, port = build_server(graph, HOST, 0, result_ttl_s=result_ttl_s)
    server.start()
    car.start()
    services.register(graph, car, agent)
    channel = grpc.insecure_channel("%s:%d" % (HOST, port))
    grpc.channel_ready_future(channel).result(timeout=5.0)
    try:
        yield SimpleNamespace(
            graph=graph, world=world, car=car, agent=agent, port=port,
            channel=channel, stub=pb_grpc.Ros2BridgeStub(channel),
        )
    finally:
        channel.close()
        car.stop()
        stop_server(server, grace=0.5)


def call(stub, name, payload=b"", encoding="cdr"):
    response = stub.CallService(
        pb.CallServiceRequest(name=name, request=payload, encoding=encoding)
    )
    assert response.success, "%s failed: %s" % (name, response.error)
    return response


def trigger_response(response):
    return m.decode(bytes(response.response), "std_srvs/srv/Trigger_Response")


def wait_for(predicate, timeout: float = 10.0, interval: float = 0.05) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return bool(predicate())


def follow_goal(stub, name, goal_id, *, limit=200):
    """Collect (feedback dicts, terminal message) from an ActionFeedback stream."""
    stream = stub.ActionFeedback(pb.ActionFeedbackRequest(name=name, goal_id=goal_id))
    feedbacks = []
    for message in stream:
        if message.feedback:
            feedbacks.append(message)
        if message.status in TERMINAL:
            return feedbacks, message
        assert len(feedbacks) < limit, "goal produced %d feedback messages" % len(feedbacks)
    raise AssertionError("the feedback stream ended without a terminal status")


# --------------------------------------------------------------------------
# services
# --------------------------------------------------------------------------

def test_list_services_shows_the_sim_services():
    with bridge_stack() as stack:
        listing = stack.stub.ListServices(pb.ListServicesRequest())
        found = {service.name: service.type for service in listing.services}
        assert found == {
            "/sim/enable": "nav2_msgs/srv/Toggle",
            "/sim/pause": "std_srvs/srv/Empty",
            "/sim/reset": "std_srvs/srv/Trigger",
            "/sim/resume": "std_srvs/srv/Empty",
            "/sim/set_initial_pose": "nav2_msgs/srv/SetInitialPose",
            "/sim/set_paused": "std_srvs/srv/SetBool",
            "/sim/status": "std_srvs/srv/Trigger",
        }, found
        assert all(service.source == "local" for service in listing.services)


def test_list_services_honours_the_name_filter_regex():
    with bridge_stack() as stack:
        response = stack.stub.ListServices(pb.ListServicesRequest(name_filter="^/sim/(pause|resume)$"))
        assert sorted(service.name for service in response.services) == ["/sim/pause", "/sim/resume"]


def test_get_service_info_reports_the_type():
    with bridge_stack() as stack:
        info = stack.stub.GetServiceInfo(pb.GetServiceInfoRequest(name="/sim/status"))
        assert info.name == "/sim/status"
        assert info.type == "std_srvs/srv/Trigger"


def test_call_service_decodes_a_trigger_response():
    with bridge_stack() as stack:
        response = call(stack.stub, "/sim/status")
        assert response.encoding == "cdr"
        decoded = trigger_response(response)
        assert decoded["success"] is True
        assert "theta" in decoded["message"], decoded["message"]


def test_call_service_with_an_empty_request_round_trips():
    with bridge_stack() as stack:
        empty = m.encode({}, "std_srvs/srv/Empty_Request")
        # Not a bare encapsulation header: a body shorter than four bytes is
        # padded out to four, which is what rmw_fastrtps_cpp puts on the wire
        # (measured on Jazzy - `serialize_message(Empty())` is exactly this)
        # and what the Windows bridge does too (transcode.hpp). The unpadded
        # four byte form is what this test used to send, and it is the reason
        # the bug below went unnoticed for so long.
        assert empty == PADDED_EMPTY_REQUEST, empty.hex()

        call(stack.stub, "/sim/pause", payload=empty)
        assert stack.car.paused is True, "the pause service did not pause the car"
        call(stack.stub, "/sim/resume", payload=empty)
        assert stack.car.paused is False, "the resume service did not resume the car"


def test_an_empty_request_written_by_the_bridge_is_accepted():
    """The payload `ros2 service call` puts on the wire from the Windows side.

    Without the padding above the agent answered "undecodable
    std_srvs/srv/Trigger_Request request: 4 trailing byte(s)", the daemon
    counted a *failure* and never a call, and every cross-machine
    `ros2 service call` to a field-less request timed out five seconds later
    with "the service did not run". A request that carries fields was never
    affected - only the ones whose whole body is the padding
    (bridge/docs/03-validation.md, evidence 59).
    """
    with bridge_stack() as stack:
        assert m.decode(PADDED_EMPTY_REQUEST, "std_srvs/srv/Trigger_Request") == {}
        decoded = trigger_response(
            call(stack.stub, "/sim/reset", payload=PADDED_EMPTY_REQUEST))
        assert decoded["success"] is True
        x, y, _ = stack.car.pose
        assert abs(x) < 1e-9 and abs(y) < 1e-9, (x, y)


def test_the_empty_request_tolerance_is_exactly_the_padding():
    """It accepts the padding, not "whatever follows the fields"."""
    with bridge_stack() as stack:
        with pytest.raises(m.CdrError):
            m.decode(PADDED_EMPTY_REQUEST[:-1] + b"\x01", "std_srvs/srv/Trigger_Request")
        with pytest.raises(m.CdrError):
            m.decode(PADDED_EMPTY_REQUEST + b"\x00", "std_srvs/srv/Trigger_Request")
        # A request that does have a field is not padded to anything, so a
        # byte after it is still an error.
        set_bool = m.encode({"data": True}, "std_srvs/srv/SetBool_Request")
        assert len(set_bool) == 8, set_bool.hex()
        with pytest.raises(m.CdrError):
            m.decode(set_bool + b"\x00", "std_srvs/srv/SetBool_Request")


def test_reset_service_puts_the_robot_back_at_the_origin():
    with bridge_stack() as stack:
        stack.car.set_pose(3.0, -2.0, 1.0)
        stack.car.set_cmd(0.5, 0.0)
        decoded = trigger_response(call(stack.stub, "/sim/reset", payload=m.encode({}, "std_srvs/srv/Empty_Request")))
        assert decoded["success"] is True
        x, y, _ = stack.car.pose
        assert abs(x) < 1e-9 and abs(y) < 1e-9, (x, y)
        assert stack.car.cmd == (0.0, 0.0), "reset left a command behind"


def test_set_initial_pose_moves_the_robot_where_the_caller_asked():
    # A `PoseWithCovarianceStamped` in a service *request*: two nested messages
    # plus a 36-element fixed array, i.e. the deepest thing the bridge carries.
    with bridge_stack() as stack:
        request = m.encode(
            {"pose": {
                "header": m.make_header("map", 1234, 5678),
                "pose": {"pose": m.make_pose(2.0, -1.0, 0.5), "covariance": [0.25] * 36},
            }},
            "nav2_msgs/srv/SetInitialPose_Request",
        )
        response = call(stack.stub, "/sim/set_initial_pose", payload=request)
        # The response has no fields, and it still comes back as CDR: an empty
        # payload is not something a caller can read.
        assert m.decode(bytes(response.response), "nav2_msgs/srv/SetInitialPose_Response") == {}

        x, y, theta = stack.car.pose
        assert abs(x - 2.0) < 1e-6 and abs(y + 1.0) < 1e-6, (x, y)
        assert abs(theta - 0.5) < 1e-6, theta


def test_toggle_stops_and_restarts_the_robot():
    with bridge_stack() as stack:
        request = m.encode({"enable": False}, "nav2_msgs/srv/Toggle_Request")
        response = call(stack.stub, "/sim/enable", payload=request)
        decoded = m.decode(bytes(response.response), "nav2_msgs/srv/Toggle_Response")
        assert decoded["success"] is True, decoded
        assert "disabled" in decoded["message"], decoded
        assert stack.car.paused is True

        request = m.encode({"enable": True}, "nav2_msgs/srv/Toggle_Request")
        response = call(stack.stub, "/sim/enable", payload=request)
        decoded = m.decode(bytes(response.response), "nav2_msgs/srv/Toggle_Response")
        assert decoded["success"] is True, decoded
        assert stack.car.paused is False


def test_call_service_rejects_bad_input_without_dying():
    with bridge_stack() as stack:
        unknown = stack.stub.CallService(pb.CallServiceRequest(name="/nope"))
        assert not unknown.success and "/nope" in unknown.error

        truncated = stack.stub.CallService(
            pb.CallServiceRequest(name="/sim/status", request=b"\x00\x01", encoding="cdr")
        )
        assert not truncated.success, "a 2 byte payload is not a CDR message"

        # A request that *does* have fields cannot be left empty: the caller
        # would otherwise be silently asking for the field's default.
        empty_body = stack.stub.CallService(pb.CallServiceRequest(name="/sim/set_paused"))
        assert not empty_body.success, "a SetBool request has a field to fill in"
        assert "SetBool_Request" in empty_body.error, empty_body.error

        wrong_encoding = stack.stub.CallService(
            pb.CallServiceRequest(name="/sim/status", encoding="shim")
        )
        assert not wrong_encoding.success and "encoding" in wrong_encoding.error

        assert stack.stub.Ping(pb.PingRequest(payload="alive")).payload == "alive"


def test_set_bool_service_uses_the_request_field():
    with bridge_stack() as stack:
        request = m.encode({"data": True}, "std_srvs/srv/SetBool_Request")
        assert trigger_response(call(stack.stub, "/sim/set_paused", payload=request))["success"] is True
        assert stack.car.paused is True

        request = m.encode({"data": False}, "std_srvs/srv/SetBool_Request")
        call(stack.stub, "/sim/set_paused", payload=request)
        assert stack.car.paused is False


def test_a_failing_service_handler_does_not_kill_the_server():
    with bridge_stack() as stack:
        def explode(_request):
            raise RuntimeError("handler is broken")

        stack.graph.create_service("/sim/explode", "std_srvs/srv/Trigger", explode)
        response = stack.stub.CallService(
            pb.CallServiceRequest(name="/sim/explode", request=b"", encoding="cdr")
        )
        assert not response.success
        assert "handler is broken" in response.error
        assert stack.stub.Ping(pb.PingRequest(payload="alive")).payload == "alive"


# --------------------------------------------------------------------------
# actions
# --------------------------------------------------------------------------

def test_list_actions_shows_every_action_the_sim_serves():
    with bridge_stack() as stack:
        listing = stack.stub.ListActions(pb.ListActionsRequest())
        # `graph.actions()` is sorted by name, the way a `ros2 action list`
        # prints, so this is alphabetical and not the order they were
        # registered in.
        assert [(action.name, action.type) for action in listing.actions] == [
            ("/sim/back_up", "nav2_msgs/action/BackUp"),
            ("/sim/drive", "ros2_bridge_sim/action/Drive"),
            ("/sim/drive_on_heading", "nav2_msgs/action/DriveOnHeading"),
            ("/sim/spin", "nav2_msgs/action/Spin"),
            ("/sim/wait", "nav2_msgs/action/Wait"),
        ]
        info = stack.stub.GetActionInfo(pb.GetActionInfoRequest(name="/sim/drive"))
        assert info.type == "ros2_bridge_sim/action/Drive"
        spin = stack.stub.GetActionInfo(pb.GetActionInfoRequest(name="/sim/spin"))
        assert spin.type == "nav2_msgs/action/Spin"


def send_nav2_goal(stack, name: str, type_name: str, fields: dict):
    """Send a goal built from a nav2 action type, the way a caller does."""
    goal = m.encode(fields, type_name + "_Goal")
    sent = stack.stub.SendActionGoal(
        pb.SendActionGoalRequest(name=name, goal=goal, encoding="cdr")
    )
    assert sent.accepted, sent.error
    return sent


def test_spin_turns_the_robot_in_place():
    # `Spin` is the recovery the default behavior tree reaches for first, and
    # the one whose goal carries a `builtin_interfaces/Duration` - a nested
    # message the bridge had never carried in an action payload before.
    with bridge_stack() as stack:
        sent = send_nav2_goal(
            stack, "/sim/spin", "nav2_msgs/action/Spin",
            {"target_yaw": 1.0,
             "time_allowance": {"sec": 0, "nanosec": 0},
             "disable_collision_checks": True},
        )
        feedbacks, terminal = follow_goal(stack.stub, "/sim/spin", sent.goal_id)
        assert terminal.status == pb.GOAL_STATUS_SUCCEEDED, terminal.error
        assert len(feedbacks) >= 3, "expected live progress, got %d messages" % len(feedbacks)

        first = m.decode(bytes(feedbacks[0].feedback), "nav2_msgs/action/Spin_Feedback")
        last = m.decode(bytes(feedbacks[-1].feedback), "nav2_msgs/action/Spin_Feedback")
        assert last["angular_distance_traveled"] > first["angular_distance_traveled"], (first, last)
        assert abs(last["angular_distance_traveled"] - 1.0) < 0.2, last

        result = m.decode(bytes(terminal.result), "nav2_msgs/action/Spin_Result")
        assert result["error_code"] == 0, result
        _, _, theta = stack.car.pose
        assert abs(theta - 1.0) < 0.25, "the robot did not turn where the result says"


def test_back_up_drives_the_robot_backwards():
    with bridge_stack() as stack:
        sent = send_nav2_goal(
            stack, "/sim/back_up", "nav2_msgs/action/BackUp",
            {"target": {"x": -0.4, "y": 0.0, "z": 0.0},
             "speed": -0.2,
             "time_allowance": {"sec": 0, "nanosec": 0},
             "disable_collision_checks": True},
        )
        feedbacks, terminal = follow_goal(stack.stub, "/sim/back_up", sent.goal_id)
        assert terminal.status == pb.GOAL_STATUS_SUCCEEDED, terminal.error
        assert len(feedbacks) >= 3

        last = m.decode(bytes(feedbacks[-1].feedback), "nav2_msgs/action/BackUp_Feedback")
        assert abs(last["distance_traveled"] - 0.4) < 0.1, last

        result = m.decode(bytes(terminal.result), "nav2_msgs/action/BackUp_Result")
        assert result["error_code"] == 0, result
        x, _, theta = stack.car.pose
        assert x < -0.3, "the car did not back up: x=%.3f" % x
        # Straight back, no turning: a backup that rotated the car would make
        # the distance in the result meaningless.
        assert abs(theta) < 0.05, theta


def test_a_behavior_reports_a_goal_it_cannot_serve():
    # Upstream rejects a `target.x` / `speed` pair that disagree on the
    # direction with INVALID_INPUT - and reports it as a *result*, not as an
    # aborted goal: the behavior tree reads the error code to decide what to do
    # next, so the goal ending as SUCCEEDED is the faithful behaviour.
    with bridge_stack() as stack:
        sent = send_nav2_goal(
            stack, "/sim/back_up", "nav2_msgs/action/BackUp",
            {"target": {"x": -0.5, "y": 0.0, "z": 0.0},
             "speed": 0.2,
             "time_allowance": {"sec": 0, "nanosec": 0},
             "disable_collision_checks": True},
        )
        _, terminal = follow_goal(stack.stub, "/sim/back_up", sent.goal_id)
        assert terminal.status == pb.GOAL_STATUS_SUCCEEDED, terminal.error
        result = m.decode(bytes(terminal.result), "nav2_msgs/action/BackUp_Result")
        assert result["error_code"] == 713, result
        assert "sign" in result["error_msg"], result
        assert stack.car.cmd == (0.0, 0.0), "a rejected goal moved the robot"


def test_wait_reports_the_time_it_has_left():
    with bridge_stack() as stack:
        sent = send_nav2_goal(
            stack, "/sim/wait", "nav2_msgs/action/Wait",
            {"time": {"sec": 0, "nanosec": 500000000}},
        )
        feedbacks, terminal = follow_goal(stack.stub, "/sim/wait", sent.goal_id)
        assert terminal.status == pb.GOAL_STATUS_SUCCEEDED, terminal.error
        assert feedbacks, "waiting produced no feedback at all"

        seconds = [
            m.decode(bytes(message.feedback), "nav2_msgs/action/Wait_Feedback")["time_left"]
            for message in feedbacks
        ]
        left = [value["sec"] + value["nanosec"] * 1e-9 for value in seconds]
        assert left == sorted(left, reverse=True), left
        assert 0.0 < left[0] <= 0.5, left

        result = m.decode(bytes(terminal.result), "nav2_msgs/action/Wait_Result")
        assert result["error_code"] == 0, result
        elapsed = result["total_elapsed_time"]
        total = elapsed["sec"] + elapsed["nanosec"] * 1e-9
        assert 0.4 <= total <= 1.5, total


def test_send_goal_streams_feedback_and_returns_a_result():
    with bridge_stack() as stack:
        goal = m.encode({"distance": 1.2}, "ros2_bridge_sim/action/Drive_Goal")
        sent = stack.stub.SendActionGoal(
            pb.SendActionGoalRequest(name="/sim/drive", goal=goal, encoding="cdr")
        )
        assert sent.accepted, sent.error
        assert sent.goal_id, "an accepted goal must come back with a handle"

        feedbacks, terminal = follow_goal(stack.stub, "/sim/drive", sent.goal_id)
        assert terminal.status == pb.GOAL_STATUS_SUCCEEDED, terminal.error
        assert len(feedbacks) >= 3, "expected live progress, got %d messages" % len(feedbacks)

        first = m.decode(bytes(feedbacks[0].feedback), "ros2_bridge_sim/action/Drive_Feedback")
        last = m.decode(bytes(feedbacks[-1].feedback), "ros2_bridge_sim/action/Drive_Feedback")
        assert last["travelled"] > first["travelled"], (first, last)
        assert terminal.encoding == "cdr"

        result = m.decode(bytes(terminal.result), "ros2_bridge_sim/action/Drive_Result")
        assert result["success"] is True, "drive action reported %r" % (terminal.error or result)
        assert result["travelled"] >= 1.2, result

        x, y, _ = stack.car.pose
        assert abs(x - result["travelled"]) < 0.15, "the robot did not go where the result says"


def test_get_action_result_polls_a_finished_goal():
    with bridge_stack() as stack:
        goal = m.encode({"distance": 0.4}, "ros2_bridge_sim/action/Drive_Goal")
        sent = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
        assert sent.accepted

        response = stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id=sent.goal_id, timeout_ms=8000)
        )
        assert response.success, response.error
        assert response.status == pb.GOAL_STATUS_SUCCEEDED
        assert m.decode(bytes(response.result), "ros2_bridge_sim/action/Drive_Result")["success"] is True


def test_get_action_result_reports_a_goal_that_is_still_running():
    with bridge_stack() as stack:
        goal = m.encode({"distance": 30.0}, "ros2_bridge_sim/action/Drive_Goal")
        sent = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
        assert sent.accepted
        try:
            response = stack.stub.GetActionResult(
                pb.GetActionResultRequest(name="/sim/drive", goal_id=sent.goal_id)
            )
            assert not response.success
            assert response.status in (pb.GOAL_STATUS_ACCEPTED, pb.GOAL_STATUS_EXECUTING), response
        finally:
            stack.stub.CancelActionGoal(
                pb.CancelActionGoalRequest(name="/sim/drive", goal_id=sent.goal_id)
            )


def test_cancel_ends_a_goal_as_canceled():
    with bridge_stack() as stack:
        goal = m.encode({"distance": 30.0}, "ros2_bridge_sim/action/Drive_Goal")
        sent = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
        assert sent.accepted
        time.sleep(0.5)

        cancelled = stack.stub.CancelActionGoal(
            pb.CancelActionGoalRequest(name="/sim/drive", goal_id=sent.goal_id)
        )
        assert cancelled.accepted, cancelled.error

        response = stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id=sent.goal_id, timeout_ms=8000)
        )
        assert response.status == pb.GOAL_STATUS_CANCELED, response
        assert wait_for(lambda: stack.car.cmd == (0.0, 0.0), 5.0), "cancel left the robot driving"

        # A cancelled goal still ends *with a result*, and it is the one the
        # handler produced: how far the robot got before it stopped. This used
        # to go out empty - `finish()` was handed the very attribute it was
        # about to store - and an empty payload is not something a caller on
        # the far side of the bridge can read, so it sat out its whole timeout
        # waiting for a goal that was already over.
        result = m.decode(bytes(response.result), "ros2_bridge_sim/action/Drive_Result")
        assert result["success"] is False, result
        x, y, _ = stack.car.pose
        assert result["travelled"] > 0.0, "the result lost the distance the robot covered"
        assert abs(result["travelled"] - math.hypot(x, y)) < 0.15, (result, stack.car.pose)


def test_unknown_goals_and_actions_are_reported_cleanly():
    with bridge_stack() as stack:
        # Sending a goal to an action nobody serves is NOT_FOUND, the same
        # answer a typo in `ros2 action send_goal` gets on a real graph.
        try:
            stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/nope"))
        except grpc.RpcError as error:
            assert error.code() == grpc.StatusCode.NOT_FOUND, error
            assert "/nope" in error.details(), error.details()
        else:
            raise AssertionError("SendActionGoal to an unknown action should fail")

        unknown = stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id="deadbeef")
        )
        assert not unknown.success and "deadbeef" in unknown.error

        missing = stack.stub.CancelActionGoal(
            pb.CancelActionGoalRequest(name="/sim/drive", goal_id="deadbeef")
        )
        assert not missing.accepted


def test_finished_goals_stay_collectable_for_their_ttl():
    with bridge_stack(result_ttl_s=30.0) as stack:
        ids = []
        for _ in range(2):
            goal = m.encode({"distance": 0.2}, "ros2_bridge_sim/action/Drive_Goal")
            sent = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
            assert sent.accepted
            ids.append(sent.goal_id)
            stack.stub.GetActionResult(
                pb.GetActionResultRequest(name="/sim/drive", goal_id=sent.goal_id, timeout_ms=8000)
            )
        # A goal whose result has been handed out stays around for a while, so
        # a client that polls late still finds it.
        assert stack.agent.goal_count == 2, stack.agent.goal_count
        late = stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id=ids[0])
        )
        assert late.success, "the first goal stopped answering: %s" % late.error


def test_stale_goals_are_pruned():
    with bridge_stack(result_ttl_s=0.0) as stack:
        goal = m.encode({"distance": 0.2}, "ros2_bridge_sim/action/Drive_Goal")
        first = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
        stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id=first.goal_id, timeout_ms=8000)
        )
        assert stack.agent.goal_count == 1

        # The next goal is what triggers the sweep, and with a zero TTL the
        # finished one is gone the moment the new goal arrives.
        second = stack.stub.SendActionGoal(pb.SendActionGoalRequest(name="/sim/drive", goal=goal))
        assert second.accepted
        assert stack.agent.goal_count == 1, "stale goals were not pruned: %d" % stack.agent.goal_count
        assert not stack.stub.GetActionResult(
            pb.GetActionResultRequest(name="/sim/drive", goal_id=first.goal_id)
        ).success, "a pruned goal should no longer answer"


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
    print("%d failed" % failures if failures else "all service/action end-to-end tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
