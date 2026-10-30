"""QoS under rclpy's names, on top of :mod:`ros2py.qos`.

Two spellings of the same three policies: rclpy says ``QoSReliabilityPolicy``,
the shim's own module says ``ReliabilityPolicy``. Both are exported and they
are the *same objects*, not copies - a script that mixes the two spellings in
one file has to keep working, which is what a ported script does.

What a QoS argument means on the shim is in ``ros2py.qos``: it is parsed, and
the depth is used to size the topic's ring; the rest is declared and not
enforced, because there is no discovery to negotiate it with.
"""
from ros2py.qos import (  # noqa: F401
    DurabilityPolicy,
    HistoryPolicy,
    LivelinessPolicy,
    QoSProfile,
    ReliabilityPolicy,
    profile_from,
    qos_profile_action_status_default,
    qos_profile_default,
    qos_profile_parameter_events,
    qos_profile_parameters,
    qos_profile_sensor_data,
    qos_profile_services_default,
    qos_profile_system_default,
    qos_profile_unknown,
)

QoSReliabilityPolicy = ReliabilityPolicy
QoSDurabilityPolicy = DurabilityPolicy
QoSHistoryPolicy = HistoryPolicy
QoSLivelinessPolicy = LivelinessPolicy

__all__ = [
    "DurabilityPolicy",
    "HistoryPolicy",
    "LivelinessPolicy",
    "QoSProfile",
    "QoSDurabilityPolicy",
    "QoSHistoryPolicy",
    "QoSLivelinessPolicy",
    "QoSReliabilityPolicy",
    "ReliabilityPolicy",
    "profile_from",
    "qos_profile_action_status_default",
    "qos_profile_default",
    "qos_profile_parameter_events",
    "qos_profile_parameters",
    "qos_profile_sensor_data",
    "qos_profile_services_default",
    "qos_profile_system_default",
    "qos_profile_unknown",
]
