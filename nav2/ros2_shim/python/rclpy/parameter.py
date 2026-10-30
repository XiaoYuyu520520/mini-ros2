"""``Parameter``, re-exported: the implementation is in :mod:`ros2py.node`.

The shim's parameters are the ones a node declares and nothing more - there is
no parameter service to talk to another process' - so this is a name and a
value, and ``Parameter.Type`` is only here because a script may name it.
"""
from ros2py.node import Parameter  # noqa: F401


class ParameterType:
    """rclpy's ``Parameter.Type``, for a script that spells a type out."""

    NOT_SET = 0
    BOOL = 1
    INTEGER = 2
    DOUBLE = 3
    STRING = 4
    BYTE_ARRAY = 5
    BOOL_ARRAY = 6
    INTEGER_ARRAY = 7
    DOUBLE_ARRAY = 8
    STRING_ARRAY = 9


Parameter.Type = ParameterType

__all__ = ["Parameter", "ParameterType"]
