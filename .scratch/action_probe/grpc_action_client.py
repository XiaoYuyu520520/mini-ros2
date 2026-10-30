"""Drives the four action RPCs over real gRPC against the agent on 50111.

Unlike the C++ test - which starts its own action server and talks to the
backend directly - this goes the whole way: gRPC client -> agent -> rclcpp
backend -> the live `action_tutorials_cpp` Fibonacci server on the robot.
"""
import struct
import sys
import threading
import time

sys.path.insert(0, "/home/ubuntu/code/CPU/bridge/ubuntu/ros2_bridge_sim")
import grpc  # noqa: E402
from proto import ros2_bridge_pb2 as pb  # noqa: E402
from proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402

ACTION = "/fibonacci"
TYPE = "action_tutorials_interfaces/action/Fibonacci"
failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def goal_cdr(order):
    # CDR encapsulation header: representation identifier 0x0001 for
    # little-endian, big-endian on the wire, then the options word.
    return b"\x00\x01\x00\x00" + struct.pack("<i", order)


def decode_sequence(blob):
    length = struct.unpack_from("<i", blob, 4)[0]
    return list(struct.unpack_from("<%di" % length, blob, 8))


channel = grpc.insecure_channel("localhost:50111")
stub = pb_grpc.Ros2BridgeStub(channel)
grpc.channel_ready_future(channel).result(timeout=10)

listed = stub.ListActions(pb.ListActionsRequest())
found = [a for a in listed.actions if a.name == ACTION]
check(bool(found), "ListActions reports %s" % ACTION)
if found:
    check(found[0].type == TYPE, "the action type is %s (got %s)" % (TYPE, found[0].type))

# ---- a goal that runs to completion
sent = stub.SendActionGoal(
    pb.SendActionGoalRequest(name=ACTION, type=TYPE, goal=goal_cdr(5), encoding="cdr",
                             timeout_ms=8000))
check(sent.accepted, "SendActionGoal accepted the goal")
check(len(sent.goal_id) == 32, "goal id is 32 hex digits (%s)" % sent.goal_id)

feedbacks = []
terminal = []


def follow():
    for message in stub.ActionFeedback(
            pb.ActionFeedbackRequest(name=ACTION, goal_id=sent.goal_id)):
        if message.status == pb.GOAL_STATUS_EXECUTING:
            feedbacks.append(message.feedback)
        else:
            terminal.append(message)


thread = threading.Thread(target=follow)
thread.start()
thread.join(timeout=30)
check(not thread.is_alive(), "ActionFeedback stream ended on its own")
check(len(feedbacks) > 0, "feedback arrived (%d messages)" % len(feedbacks))
check(len(terminal) == 1, "exactly one terminal message (%d)" % len(terminal))
if terminal:
    last = terminal[0]
    check(last.status == pb.GOAL_STATUS_SUCCEEDED, "terminal status is SUCCEEDED")
    check(last.result != b"", "the terminal message carries a result")
    if last.result:
        sequence = decode_sequence(last.result)
        check(sequence == [0, 1, 1, 2, 3, 5], "result sequence is %s" % sequence)

result = stub.GetActionResult(
    pb.GetActionResultRequest(name=ACTION, goal_id=sent.goal_id, timeout_ms=8000))
check(result.success, "GetActionResult reports success")
check(result.status == pb.GOAL_STATUS_SUCCEEDED, "GetActionResult status is SUCCEEDED")
if result.result:
    check(decode_sequence(result.result) == [0, 1, 1, 2, 3, 5],
          "GetActionResult sequence is %s" % decode_sequence(result.result))

# ---- cancel a goal that is still running
long_goal = stub.SendActionGoal(
    pb.SendActionGoalRequest(name=ACTION, type=TYPE, goal=goal_cdr(40), encoding="cdr",
                             timeout_ms=8000))
check(long_goal.accepted, "a long goal was accepted")
cancelled = []
cancel_terminal = []


def follow_long():
    for message in stub.ActionFeedback(
            pb.ActionFeedbackRequest(name=ACTION, goal_id=long_goal.goal_id)):
        if message.status != pb.GOAL_STATUS_EXECUTING:
            cancel_terminal.append(message)


thread = threading.Thread(target=follow_long)
thread.start()
time.sleep(0.4)
answer = stub.CancelActionGoal(
    pb.CancelActionGoalRequest(name=ACTION, goal_id=long_goal.goal_id))
check(answer.accepted, "CancelActionGoal accepted the cancel")
thread.join(timeout=30)
check(not thread.is_alive(), "the canceled goal's stream ended on its own")
check(len(cancel_terminal) == 1, "the canceled goal reported a terminal message")
if cancel_terminal:
    check(cancel_terminal[0].status == pb.GOAL_STATUS_CANCELED,
          "the terminal status is CANCELED (got %d)" % cancel_terminal[0].status)

# ---- a goal id nobody knows
try:
    stub.GetActionResult(pb.GetActionResultRequest(
        name=ACTION, goal_id="0" * 32, timeout_ms=1000))
    check(False, "an unknown goal id is refused")
except grpc.RpcError as error:
    check(error.code() == grpc.StatusCode.NOT_FOUND,
          "an unknown goal id is NOT_FOUND (got %s)" % error.code())

print("failures=%d" % len(failures))
sys.exit(1 if failures else 0)
