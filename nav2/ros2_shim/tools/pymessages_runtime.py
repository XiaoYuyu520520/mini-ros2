"""Wire-format runtime for generated ROS2 Python message classes.

Must stay byte-for-byte compatible with ros2_shim/serialization.hpp:
  integral / float : little endian raw bytes
  bool             : 1 byte
  string           : uint32 length + utf-8 bytes
  wstring          : uint32 length + utf-16-le units
  sequence<T>      : uint32 count + elements
  array<T,N>       : N elements
  message          : fields in declaration order
"""

import struct

FMT = {"b": "b", "B": "B", "h": "h", "H": "H", "i": "i", "I": "I",
       "q": "q", "Q": "Q", "f": "f", "d": "d", "?": "?"}


def f_scalar(code):
    return ("scalar", code)


def f_bool():
    return ("bool",)


def f_string():
    return ("string",)


def f_wstring():
    return ("wstring",)


def f_msg(cls):
    return ("msg", cls)


def f_seq(element):
    return ("seq", element)


def f_array(element, count):
    return ("array", element, count)


class Msg:
    _fields = ()

    def serialize(self):
        out = bytearray()
        _write_msg(out, self)
        return bytes(out)

    @classmethod
    def deserialize(cls, data, offset=0):
        obj = cls.__new__(cls)
        offset = _read_msg(data, offset, obj)
        return obj, offset

    @classmethod
    def from_bytes(cls, data):
        obj, _ = cls.deserialize(data, 0)
        return obj

    def __eq__(self, other):
        return type(self) is type(other) and _msg_equal(self, other)

    def __repr__(self):
        values = ", ".join(f"{name}={getattr(self, name)!r}" for name, _ in self._fields)
        return f"{type(self).__name__}({values})"


def _msg_equal(a, b):
    for name, _ in type(a)._fields:
        if getattr(a, name) != getattr(b, name):
            return False
    return True


def _write_msg(out, msg):
    for name, desc in type(msg)._fields:
        _write(out, getattr(msg, name), desc)


def _write(out, value, desc):
    kind = desc[0]
    if kind == "scalar":
        out += struct.pack("<" + desc[1], value)
    elif kind == "bool":
        out += b"\x01" if value else b"\x00"
    elif kind == "string":
        data = value.encode("utf-8")
        out += struct.pack("<I", len(data))
        out += data
    elif kind == "wstring":
        data = value.encode("utf-16-le")
        out += struct.pack("<I", len(value))
        out += data
    elif kind == "msg":
        _write_msg(out, value)
    elif kind == "seq":
        out += struct.pack("<I", len(value))
        element = desc[1]
        for item in value:
            _write(out, item, element)
    elif kind == "array":
        element = desc[1]
        for item in value:
            _write(out, item, element)
    else:
        raise ValueError(f"unknown field kind {kind!r}")


def _read_msg(data, offset, obj):
    for name, desc in type(obj)._fields:
        value, offset = _read(data, offset, desc)
        setattr(obj, name, value)
    return offset


def _read(data, offset, desc):
    kind = desc[0]
    if kind == "scalar":
        value = struct.unpack_from("<" + desc[1], data, offset)[0]
        return value, offset + struct.calcsize(desc[1])
    if kind == "bool":
        return data[offset] != 0, offset + 1
    if kind == "string":
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        return data[offset:offset + length].decode("utf-8"), offset + length
    if kind == "wstring":
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        return data[offset:offset + 2 * length].decode("utf-16-le"), offset + 2 * length
    if kind == "msg":
        cls = desc[1]
        return cls.deserialize(data, offset)
    if kind == "seq":
        count = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        element = desc[1]
        items = []
        for _ in range(count):
            value, offset = _read(data, offset, element)
            items.append(value)
        return items, offset
    if kind == "array":
        element = desc[1]
        count = desc[2]
        items = []
        for _ in range(count):
            value, offset = _read(data, offset, element)
            items.append(value)
        return items, offset
    raise ValueError(f"unknown field kind {kind!r}")


# ---------------------------------------------------------------------------
# Action classes: the `Impl` namespace ROS2's generator hangs off them.
# ---------------------------------------------------------------------------

def build_action_impl(action_class):
    """The `Impl` namespace an action class carries upstream.

    rclpy's generator puts five more types under ``<Action>.Impl``: the two
    services the action is built from (``SendGoalService``, ``GetResultService``),
    the two generic ones every action shares (``CancelGoalService``,
    ``GoalStatusMessage``), and ``FeedbackMessage`` - the wrapper with a
    ``goal_id`` and a ``feedback`` field that a feedback callback is handed.

    None of them travel on the shim's wire, where an action is six channels
    carrying an id and the goal, feedback or result. They are here because
    Python evaluates an annotation when the *function* is defined, not when it
    is called: ``nav2_simple_commander`` writes

        def _feedback_callback(self, msg: ComputeAndTrackRoute.Impl.FeedbackMessage)

    inside a class body, so importing that library needs the name to resolve.
    It is also the attribute a ported node reads ``.feedback`` off.

    Built by assignment rather than emitted as nested classes, because a nested
    class body cannot see the names of the class body around it - ``Goal`` is
    not bound yet while ``Impl`` would be created.
    """
    from pymessages.action_msgs.msg.goal_status_array import GoalStatusArray
    from pymessages.action_msgs.srv.cancel_goal import CancelGoal
    from pymessages.builtin_interfaces.msg.time import Time
    from pymessages.unique_identifier_msgs.msg.uuid import UUID

    goal_type = action_class.Goal
    result_type = action_class.Result
    feedback_type = action_class.Feedback

    class SendGoal_Request(Msg):
        _fields = [("goal_id", f_msg(UUID)), ("goal", f_msg(goal_type))]

        def __init__(self):
            self.goal_id = UUID()
            self.goal = goal_type()

    class SendGoal_Response(Msg):
        _fields = [("accepted", f_bool()), ("stamp", f_msg(Time))]

        def __init__(self):
            self.accepted = False
            self.stamp = Time()

    class GetResult_Request(Msg):
        _fields = [("goal_id", f_msg(UUID))]

        def __init__(self):
            self.goal_id = UUID()

    class GetResult_Response(Msg):
        _fields = [("status", f_scalar("b")), ("result", f_msg(result_type))]

        def __init__(self):
            self.status = 0
            self.result = result_type()

    class FeedbackMessage(Msg):
        _fields = [("goal_id", f_msg(UUID)), ("feedback", f_msg(feedback_type))]

        def __init__(self):
            self.goal_id = UUID()
            self.feedback = feedback_type()

    class SendGoalService:
        """The goal service, as the pair of messages it is made of."""

        Request = SendGoal_Request
        Response = SendGoal_Response

    class GetResultService:
        Request = GetResult_Request
        Response = GetResult_Response

    class Impl:
        pass

    Impl.SendGoalService = SendGoalService
    Impl.GetResultService = GetResultService
    Impl.FeedbackMessage = FeedbackMessage
    Impl.CancelGoalService = CancelGoal
    Impl.GoalStatusMessage = GoalStatusArray
    return Impl
