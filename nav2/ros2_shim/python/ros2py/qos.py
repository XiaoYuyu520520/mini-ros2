"""QoS, as much of it as the shim has an answer for.

The shim's transport is a ring of fixed slots with one writer per topic: every
topic is effectively ``RELIABLE``-or-better in the sense that matters (nothing
is dropped while a reader keeps up), and a slow reader that falls behind
``slot_count`` messages skips them rather than blocking the writer. There is no
discovery, so there is no per-subscription compatibility negotiation to fail -
which is why a QoS argument here is *declared* and not enforced.

It is still parsed rather than ignored, because a ROS2 script passes one:
``create_publisher(Twist, '/car_cmd_vel', 10)`` puts the depth in the third
positional slot and ``create_subscription(..., qos_profile_sensor_data)`` puts a
profile there, and a shim that only accepted one of those would fail on the
other. :func:`profile_from` is the one place that turns either into the depth
the ring is sized from.
"""
from __future__ import annotations

from enum import Enum


class HistoryPolicy(Enum):
    SYSTEM_DEFAULT = 0
    KEEP_LAST = 1
    KEEP_ALL = 2


class ReliabilityPolicy(Enum):
    SYSTEM_DEFAULT = 0
    RELIABLE = 1
    BEST_EFFORT = 2


class DurabilityPolicy(Enum):
    SYSTEM_DEFAULT = 0
    TRANSIENT_LOCAL = 1
    VOLATILE = 2


class LivelinessPolicy(Enum):
    SYSTEM_DEFAULT = 0
    AUTOMATIC = 1
    MANUAL_BY_TOPIC = 3


class QoSProfile:
    def __init__(self, **kwargs):
        self.history = kwargs.get("history", HistoryPolicy.KEEP_LAST)
        self.depth = kwargs.get("depth", 10)
        self.reliability = kwargs.get("reliability", ReliabilityPolicy.RELIABLE)
        self.durability = kwargs.get("durability", DurabilityPolicy.VOLATILE)
        self.liveliness = kwargs.get("liveliness", LivelinessPolicy.AUTOMATIC)
        self.deadline = kwargs.get("deadline", None)
        self.lifespan = kwargs.get("lifespan", None)
        self.liveliness_lease_duration = kwargs.get("liveliness_lease_duration", None)

    def __repr__(self):
        return ("QoSProfile(depth=%r, reliability=%r, durability=%r)"
                % (self.depth, self.reliability, self.durability))


qos_profile_system_default = QoSProfile(
    history=HistoryPolicy.SYSTEM_DEFAULT, depth=0,
    reliability=ReliabilityPolicy.SYSTEM_DEFAULT,
    durability=DurabilityPolicy.SYSTEM_DEFAULT)
qos_profile_sensor_data = QoSProfile(
    depth=5, reliability=ReliabilityPolicy.BEST_EFFORT,
    durability=DurabilityPolicy.VOLATILE)
qos_profile_services_default = QoSProfile(depth=10)
qos_profile_parameters = QoSProfile(depth=1000)
qos_profile_parameter_events = QoSProfile(depth=1000)
qos_profile_action_status_default = QoSProfile(depth=1)
qos_profile_unknown = QoSProfile(history=HistoryPolicy.SYSTEM_DEFAULT, depth=0)
qos_profile_default = QoSProfile(depth=10)


def profile_from(qos, default_depth: int = 10) -> QoSProfile:
    """A ``QoSProfile`` from what a ROS2 script passed in that position.

    An int is rclpy's shorthand for "keep last, this deep"; a profile is used
    as it stands; nothing at all is the default.
    """
    if qos is None:
        return QoSProfile(depth=default_depth)
    if isinstance(qos, QoSProfile):
        return qos
    if isinstance(qos, bool):
        return QoSProfile(depth=default_depth)
    if isinstance(qos, int):
        return QoSProfile(depth=max(1, qos))
    return QoSProfile(depth=default_depth)
