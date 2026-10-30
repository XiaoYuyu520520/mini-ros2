"""Time, and the clock a node stamps messages with.

These scripts ask the clock for two things: a stamp to put on a
``PoseStamped``, and a number to compare against one they read off the wire.
``waypoint_cycle`` does both - it records the moment it published a goal and
then ignores the status entries whose stamp is older than it.

That second use is why the clock is not simply the wall clock. A nav2 node on
this shim runs with ``use_sim_time`` on (``nav.yaml`` says so for every one of
them), so the stamp on ``/navigate_to_pose/_action/status`` is *simulation*
time, counted from wherever the simulator's ``/clock`` starts. Comparing that
against ``time.time()`` would put every entry in the past and the script would
never advance a waypoint - so a Python node takes the same switch a C++ one
does, and for the same reason: `use_sim_time` true, ``/clock`` followed.

The default is still the system clock. It is what a mission script on the real
car gets (``use_sim_time`` is false there), and it is what a script that passes
no `-p` gets here.
"""
from __future__ import annotations

import time as _time

_USE_SIM_TIME = [False]

# Where `/clock` says simulation time is, in nanoseconds. Zero until the first
# message lands, exactly as the C++ side reports the epoch for a node with
# `use_sim_time` on and nothing feeding it - mixing a wall-clock reading into a
# simulation timeline is worse than reporting that the timeline has not started.
_SIM_TIME_NS = [0]


def set_use_sim_time(enabled: bool) -> None:
    _USE_SIM_TIME[0] = bool(enabled)


def use_sim_time() -> bool:
    return _USE_SIM_TIME[0]


def set_sim_time(nanoseconds: int) -> None:
    """What the last ``/clock`` message carried. Called from the subscription."""
    _SIM_TIME_NS[0] = int(nanoseconds)


def get_sim_time() -> int:
    return _SIM_TIME_NS[0]


class Duration:
    """A span, in nanoseconds, with the conversions rclpy offers."""

    def __init__(self, seconds: float = 0.0, nanoseconds: int = 0):
        if seconds or not nanoseconds:
            self.nanoseconds = int(round(float(seconds) * 1e9))
        else:
            self.nanoseconds = int(nanoseconds)

    @classmethod
    def from_msg(cls, message):
        return cls(nanoseconds=message.sec * 10 ** 9 + message.nanosec)

    def to_msg(self):
        from builtin_interfaces.msg import Duration as DurationMessage
        message = DurationMessage()
        message.sec = int(self.nanoseconds // 10 ** 9)
        message.nanosec = int(self.nanoseconds % 10 ** 9)
        return message

    @property
    def seconds(self) -> float:
        return self.nanoseconds / 1e9

    def __float__(self):
        return self.seconds

    def __repr__(self):
        return "Duration(nanoseconds=%d)" % self.nanoseconds


class Time:
    """A point on the clock, with the spellings the scripts use."""

    def __init__(self, seconds: float = 0.0, nanoseconds: int = 0):
        if seconds or not nanoseconds:
            self.nanoseconds = int(round(float(seconds) * 1e9))
        else:
            self.nanoseconds = int(nanoseconds)

    @classmethod
    def from_msg(cls, message):
        return cls(nanoseconds=message.sec * 10 ** 9 + message.nanosec)

    def to_msg(self):
        from builtin_interfaces.msg import Time as TimeMessage
        message = TimeMessage()
        message.sec = int(self.nanoseconds // 10 ** 9)
        message.nanosec = int(self.nanoseconds % 10 ** 9)
        return message

    @property
    def seconds(self) -> float:
        return self.nanoseconds / 1e9

    def seconds_nanoseconds(self):
        return (int(self.nanoseconds // 10 ** 9), int(self.nanoseconds % 10 ** 9))

    def __sub__(self, other):
        if isinstance(other, Time):
            return Duration(nanoseconds=self.nanoseconds - other.nanoseconds)
        return Time(nanoseconds=self.nanoseconds - int(other.nanoseconds))

    def __add__(self, other):
        return Time(nanoseconds=self.nanoseconds + int(other.nanoseconds))

    def __repr__(self):
        return "Time(nanoseconds=%d)" % self.nanoseconds


class Clock:
    """``node.get_clock()``: where "now" comes from.

    One clock for the process rather than one per node, which is the same
    simplification the shim makes everywhere else it has to choose: a mission
    script is one node, and a script that wants a second one with a different
    idea of time is asking for something the shim does not offer.
    """

    def __init__(self, clock_type=None):
        self.clock_type = clock_type

    def now(self) -> Time:
        if _USE_SIM_TIME[0]:
            return Time(nanoseconds=_SIM_TIME_NS[0])
        return Time(nanoseconds=int(_time.time() * 1e9))

    # `get_clock_type()` is deliberately absent: the only spelling of this
    # question that means anything here is rclpy's own
    # `ClockType(Enum)`, and `ros2py` is one import below the module that
    # defines it - an int returned from here would not compare equal to it.
    def ros_time_is_active(self) -> bool:
        return _USE_SIM_TIME[0]

    def sleep_for(self, duration) -> bool:
        """Wait ``duration``; on simulated time, until ``/clock`` gets there.

        Bounded by the same span measured on the wall clock for the reason the
        C++ clock gives: a wait on a timeline nobody is feeding has to end, and
        the thread asked to wait is not always somebody else's.
        """
        seconds = float(duration)
        if not _USE_SIM_TIME[0]:
            deadline = _time.monotonic() + seconds
            while _time.monotonic() < deadline:
                _time.sleep(min(0.01, max(0.0, deadline - _time.monotonic())))
            return True
        target = _SIM_TIME_NS[0] + int(seconds * 1e9)
        wall_deadline = _time.monotonic() + seconds
        while _SIM_TIME_NS[0] < target and _time.monotonic() < wall_deadline:
            _time.sleep(0.001)
        return True
