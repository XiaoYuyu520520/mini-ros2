"""Action clients, over the six channels an action is made of.

An action in the shim is six named channels rather than a DDS service plus two
topics, and ``rclcpp_action`` on the C++ side is where that layout is decided.
Anything that wants to send a goal has to speak it exactly, because the other
end is a ported C++ server - nav2's ``waypoint_follower`` for a
``NavigateThroughPoses`` - which does not know Python exists.

What travels on each channel, every one of them with a little-endian ``uint64``
id in front. The id is the whole of the correlation, the way the 16 byte UUID
is upstream:

* ``_goal``       ``<id><serialize(Goal)>``
* ``_goal_ack``   ``<id><one byte: 0 reject, anything else accept>``
* ``_cancel``     ``<id>``
* ``_cancel_ack`` ``<id><one byte: 0 reject, 1 accept>``
* ``_feedback``   ``<id><serialize(Feedback)>`` - no status byte
* ``_result``     ``<id><one byte ResultCode><serialize(Result)>``

Four of those the client only reads, and it reads them through a handle of its
own rather than the one the node shares: two clients for one action in one
process both wait on the same ``_goal_ack``, and behind a single cursor
whichever of them polls first eats the answer that belonged to the other.
Those four are also skipped to the end when the client is built. The ring keeps
what a previous run left in it, so a client whose first goal is id 1 would
otherwise read the *previous* run's ack as the answer to it - and if that old
goal had been rejected, the new one is reported rejected although the stack is
up, which looks like nav2 refusing to drive.

Ids come from a counter that starts at one, shared by every client in the
process rather than one per client: a goal id only has to be unique against the
channel, and the channel belongs to an action name. Same reasoning as the
``static std::atomic`` in ``rclcpp_action.hpp``.
"""
from __future__ import annotations

import itertools
import struct
import time

from . import channel, graph
from .futures import Future
from .message_types import message_type_name

# rclcpp_action/types.hpp, spelled the way the C++ enum spells it, so a reader
# who has to check the two ends against each other can grep for one name.
GOAL_REJECT = 0
GOAL_ACCEPT_AND_EXECUTE = 1
GOAL_ACCEPT_AND_DEFER = 2

RESULT_UNKNOWN = 0
RESULT_SUCCEEDED = 1
RESULT_ABORTED = 2
RESULT_CANCELED = 3

# action_msgs/msg/GoalStatus: what a handle reports once a goal has been
# answered, and what a behavior tree compares against.
STATUS_UNKNOWN = 0
STATUS_ACCEPTED = 1
STATUS_EXECUTING = 2
STATUS_CANCELING = 3
STATUS_SUCCEEDED = 4
STATUS_CANCELED = 5
STATUS_ABORTED = 6

_STATUS_FOR_RESULT = {
    RESULT_SUCCEEDED: STATUS_SUCCEEDED,
    RESULT_ABORTED: STATUS_ABORTED,
    RESULT_CANCELED: STATUS_CANCELED,
}

# How many finished goals to hold for a caller that has not asked for them yet.
# A server that answers inside the same poll the ack went out in has finished
# before its client has the handle in hand, and a result that is dropped there
# is a caller waiting forever for a message that was already consumed.
MAX_COMPLETED_RESULTS = 64

# One counter for the process, not one per client - see the module docstring.
_GOAL_IDS = itertools.count(1)

# A client is usually built before the stack that serves it has finished
# activating, so a channel that is not there yet is retried rather than
# declared fatal.
REOPEN_INTERVAL = 0.2


def make_goal_id(goal_id: int) -> bytes:
    """The 16 byte UUID a goal id stands for.

    ``detail::make_uuid``: the first eight bytes are the id, the rest are zero.
    It is not a UUID and does not have to be - nothing hashes it, it only has
    to be stable and comparable, and a client and a server derive it from the
    same id independently.
    """
    return struct.pack("<Q", goal_id) + bytes(8)


def _frame(goal_id: int, payload: bytes = b"") -> bytes:
    return struct.pack("<Q", goal_id) + payload


def _id_of(data: bytes):
    if len(data) < 8:
        return None
    return struct.unpack_from("<Q", data, 0)[0]


def _drain(endpoint):
    """Everything in a ring right now, oldest first."""
    if endpoint is None:
        return
    while True:
        data = endpoint.poll()
        if data is None:
            return
        yield data


class FeedbackMessage:
    """What a feedback callback is handed: which goal, and the message."""

    def __init__(self, goal_id, feedback):
        self.goal_id = goal_id
        self.feedback = feedback

    def __repr__(self):
        return "FeedbackMessage(goal_id=%r, feedback=%r)" % (
            self.goal_id, self.feedback)


class WrappedResult:
    """The end of one goal: which one, how it ended, and what it returned."""

    def __init__(self, goal_id, status, result, code=RESULT_UNKNOWN):
        self.goal_id = goal_id
        self.status = status
        self.result = result
        self.code = code

    def __repr__(self):
        return "WrappedResult(status=%d, code=%d, result=%r)" % (
            self.status, self.code, self.result)


class GoalResponse:
    """What a server answers a goal with, as far as a client can see it."""

    REJECT = GOAL_REJECT
    ACCEPT_AND_EXECUTE = GOAL_ACCEPT_AND_EXECUTE
    ACCEPT_AND_DEFER = GOAL_ACCEPT_AND_DEFER


class CancelResponse:
    """What a server answers a cancel with."""

    REJECT = 0
    ACCEPT = 1


class CancelGoalResult:
    """What ``cancel_goal_async()`` resolves with.

    A message, not a flag: upstream the answer is ``action_msgs/srv/CancelGoal``
    's response, whose ``return_code`` distinguishes "no such goal" and "the
    goal already finished" from "the server said no", and a caller that wants
    to retry needs to be able to tell those apart.
    """

    ERROR_NONE = 0
    ERROR_REJECTED = 1
    ERROR_UNKNOWN_GOAL_ID = 2
    ERROR_GOAL_TERMINATED = 3

    def __init__(self, return_code=ERROR_NONE):
        self.return_code = return_code
        self.goals_canceling = []

    @property
    def accepted(self) -> bool:
        return self.return_code == CancelGoalResult.ERROR_NONE

    def __repr__(self):
        return "CancelGoalResult(return_code=%d)" % self.return_code


class ClientGoalHandle:
    """One sent goal. ``accepted`` is the question every mission script asks."""

    def __init__(self, client, goal_id, goal):
        self._client = client
        self._id = goal_id
        self._goal = goal
        self._accepted = False
        self._status = STATUS_UNKNOWN
        self._result_future = None

    @property
    def accepted(self) -> bool:
        return self._accepted

    @property
    def status(self) -> int:
        return self._status

    @property
    def goal_id(self) -> bytes:
        return make_goal_id(self._id)

    def get_goal(self):
        return self._goal

    def get_result_async(self) -> Future:
        """The future of this goal's :class:`WrappedResult`.

        A result that has already arrived is handed over at once instead of
        being waited for a second time: an in-process server answers inside the
        same spin that delivered the ack, and a caller that then blocks on a
        message already consumed waits forever.
        """
        future = Future()
        completed = self._client._take_completed(self._id)
        if completed is not None:
            future.set_result(completed)
        else:
            self._result_future = future
        return future

    def cancel_goal_async(self) -> Future:
        return self._client._request_cancel(self._id)

    def __repr__(self):
        return "ClientGoalHandle(id=%d, accepted=%s, status=%d)" % (
            self._id, self._accepted, self._status)


class _Pending:
    """A goal that has been sent and has not finished yet."""

    def __init__(self, handle, feedback_callback, future):
        self.handle = handle
        self.feedback_callback = feedback_callback
        self.future = future
        self.answered = False


class ActionClient:
    """``ActionClient(node, NavigateThroughPoses, 'navigate_through_poses')``."""

    def __init__(self, node, action_type, action_name, callback_group=None,
                 goal_service_qos_profile=None):
        self.node = node
        self.action_type = action_type
        self.action_name = node.resolve_name(action_name)
        self.type_name = message_type_name(action_type)
        self._goal = channel.Channel.create(self.action_name + "/_goal")
        self._goal_ack = self._reader("/_goal_ack")
        self._cancel_channel = channel.Channel.create(self.action_name + "/_cancel")
        self._cancel_ack = self._reader("/_cancel_ack")
        self._feedback_channel = self._reader("/_feedback")
        self._result_channel = self._reader("/_result")
        self._goals = {}
        self._cancel_futures = {}
        self._completed = {}
        self._last_open = time.monotonic()
        # Registered with the node, which is the only thing that will ever ask
        # this client to `poll()`. rclpy does the same through the node's wait
        # set, and without it nothing here is ever reached: the goal goes out,
        # the ack lands in the ring, and the caller waits out its timeout
        # because nobody looks.
        node._action_clients.append(self)

    def _reader(self, suffix: str):
        """Open one of the four channels a client reads, and skip to its end."""
        endpoint = channel.Channel.create(self.action_name + suffix)
        if endpoint is not None:
            endpoint.drain()
        return endpoint

    # ------------------------------------------------------------- readiness
    def server_is_ready(self) -> bool:
        return graph.action_exists(self.action_name)

    def action_server_is_ready(self) -> bool:
        return self.server_is_ready()

    def wait_for_server(self, timeout_sec=None) -> bool:
        """True once somebody serves this action.

        Answered from the graph registry rather than by watching the channels,
        which is what ``rclcpp_action::Client`` does and for the same reason: a
        caller that cannot tell sends its goal into a ring nobody is reading
        and then waits out its whole timeout, so the failure surfaces late and
        somewhere else instead of where the caller can act on it.
        """
        deadline = None if timeout_sec is None else time.monotonic() + float(timeout_sec)
        while not self.server_is_ready():
            if deadline is not None and time.monotonic() >= deadline:
                return False
            time.sleep(0.001)
        return True

    # ------------------------------------------------------------- sending
    def send_goal_async(self, goal, feedback_callback=None,
                        goal_response_callback=None) -> Future:
        """Queue a goal; the future resolves with the goal handle."""
        self._ensure_channels()
        future = Future()
        goal_id = next(_GOAL_IDS)
        handle = ClientGoalHandle(self, goal_id, goal)
        if self._goal is None:
            future.set_exception(RuntimeError(
                "no channel for %s" % (self.action_name + "/_goal")))
            return future
        if not self._goal.publish(_frame(goal_id, goal.serialize())):
            future.set_exception(RuntimeError(
                "goal for %s did not fit its channel" % self.action_name))
            return future
        self._goals[goal_id] = _Pending(handle, feedback_callback, future)
        if goal_response_callback is not None:
            future.add_done_callback(goal_response_callback)
        return future

    def _request_cancel(self, goal_id: int) -> Future:
        future = Future()
        self._ensure_channels()
        if self._cancel_channel is None:
            future.set_exception(RuntimeError(
                "no channel for %s" % (self.action_name + "/_cancel")))
            return future
        self._cancel_futures[goal_id] = future
        self._cancel_channel.publish(_frame(goal_id))
        return future

    def _take_completed(self, goal_id: int):
        return self._completed.pop(goal_id, None)

    # ------------------------------------------------------------------ poll
    def poll(self) -> int:
        """One pass over the four channels this client reads. Never blocks."""
        self._ensure_channels()
        handled = self._poll_acks()
        handled += self._poll_cancel_acks()
        handled += self._poll_feedback()
        handled += self._poll_results()
        return handled

    def _poll_acks(self) -> int:
        handled = 0
        for data in _drain(self._goal_ack):
            goal_id = _id_of(data)
            pending = self._goals.get(goal_id)
            if pending is None or pending.answered:
                continue
            pending.answered = True
            accepted = len(data) > 8 and data[8] != 0
            pending.handle._accepted = accepted
            if accepted:
                pending.handle._status = STATUS_ACCEPTED
                handled += 1
            # A rejected goal still resolves with a handle, not with nothing:
            # every mission script's next line is `if not handle.accepted`.
            pending.future.set_result(pending.handle)
        return handled

    def _poll_cancel_acks(self) -> int:
        handled = 0
        for data in _drain(self._cancel_ack):
            goal_id = _id_of(data)
            future = self._cancel_futures.pop(goal_id, None)
            if future is None:
                continue
            accepted = len(data) > 8 and data[8] != 0
            future.set_result(CancelGoalResult(
                CancelGoalResult.ERROR_NONE if accepted
                else CancelGoalResult.ERROR_REJECTED))
            handled += 1
        return handled

    def _poll_feedback(self) -> int:
        handled = 0
        for data in _drain(self._feedback_channel):
            goal_id = _id_of(data)
            pending = self._goals.get(goal_id)
            if pending is None:
                continue
            try:
                feedback = self.action_type.Feedback.from_bytes(data[8:])
            except Exception as error:  # noqa: BLE001 - a bad message is not fatal
                self.node.get_logger().error(
                    "dropped feedback on %s: %s" % (self.action_name, error))
                continue
            # Feedback is the only evidence a goal is running, which is why the
            # status moves here and not on a message of its own.
            pending.handle._status = STATUS_EXECUTING
            if pending.feedback_callback is not None:
                pending.feedback_callback(
                    FeedbackMessage(make_goal_id(goal_id), feedback))
            handled += 1
        return handled

    def _poll_results(self) -> int:
        handled = 0
        for data in _drain(self._result_channel):
            goal_id = _id_of(data)
            code = data[8] if len(data) > 8 else RESULT_UNKNOWN
            result = None
            if len(data) > 9:
                try:
                    result = self.action_type.Result.from_bytes(data[9:])
                except Exception as error:  # noqa: BLE001 - a bad message is not fatal
                    self.node.get_logger().error(
                        "dropped result on %s: %s" % (self.action_name, error))
                    continue
            self._deliver(goal_id, WrappedResult(
                make_goal_id(goal_id),
                _STATUS_FOR_RESULT.get(code, STATUS_UNKNOWN), result, code))
            handled += 1
        return handled

    def _deliver(self, goal_id: int, wrapped: WrappedResult) -> None:
        """Hand one finished goal to whoever is waiting for it."""
        pending = self._goals.pop(goal_id, None)
        cancel_future = self._cancel_futures.pop(goal_id, None)
        if cancel_future is not None:
            # The goal finished before the cancel could take effect. Answering
            # keeps the caller from waiting out its whole timeout.
            cancel_future.set_result(
                CancelGoalResult(CancelGoalResult.ERROR_GOAL_TERMINATED))
        self._remember_completed(goal_id, wrapped)
        if pending is None:
            return
        pending.handle._status = wrapped.status
        if pending.handle._result_future is not None:
            self._completed.pop(goal_id, None)
            pending.handle._result_future.set_result(wrapped)

    def _remember_completed(self, goal_id: int, wrapped: WrappedResult) -> None:
        self._completed[goal_id] = wrapped
        while len(self._completed) > MAX_COMPLETED_RESULTS:
            self._completed.pop(next(iter(self._completed)))

    def _ensure_channels(self) -> None:
        if (self._goal is not None and self._goal_ack is not None
                and self._cancel_channel is not None and self._cancel_ack is not None
                and self._feedback_channel is not None
                and self._result_channel is not None):
            return
        now = time.monotonic()
        if now - self._last_open < REOPEN_INTERVAL:
            return
        self._last_open = now
        for attribute, suffix, is_reader in (
                ("_goal", "/_goal", False),
                ("_goal_ack", "/_goal_ack", True),
                ("_cancel_channel", "/_cancel", False),
                ("_cancel_ack", "/_cancel_ack", True),
                ("_feedback_channel", "/_feedback", True),
                ("_result_channel", "/_result", True)):
            if getattr(self, attribute) is not None:
                continue
            if is_reader:
                setattr(self, attribute, self._reader(suffix))
            else:
                setattr(self, attribute, channel.Channel.create(self.action_name + suffix))

    def destroy(self) -> None:
        for endpoint in (self._goal, self._goal_ack, self._cancel_channel,
                         self._cancel_ack, self._feedback_channel,
                         self._result_channel):
            if endpoint is not None:
                endpoint.close()
        self._goal = self._goal_ack = None
        self._cancel_channel = self._cancel_ack = None
        self._feedback_channel = self._result_channel = None
        self._goals.clear()
        self._cancel_futures.clear()
        if self in self.node._action_clients:
            self.node._action_clients.remove(self)
