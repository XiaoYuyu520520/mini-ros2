"""Clocks, and the clock-type constant rclpy pairs with them.

The shim has exactly one clock and it is the system one; ``ClockType`` is here
because a script may spell it out, and ``Clock(ClockType.ROS_TIME)`` is a
request the shim answers with wall time. A script that needs ``/clock`` -
simulated time from the robot - asks for it with
:func:`ros2py.clock.set_use_sim_time`.
"""
from enum import Enum

from ros2py.clock import Clock, Duration, Time  # noqa: F401
from ros2py.clock import set_use_sim_time, use_sim_time  # noqa: F401


class ClockType(Enum):
    ROS_TIME = 1
    SYSTEM_TIME = 2
    STEADY_TIME = 3


__all__ = ["Clock", "ClockType", "Duration", "Time", "set_use_sim_time",
           "use_sim_time"]
