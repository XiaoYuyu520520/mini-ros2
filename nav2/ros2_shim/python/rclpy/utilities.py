"""The loose helpers rclpy keeps in this module."""
from __future__ import annotations

import sys

from ros2py import executor as _executor

from .context import get_default_context


def ok(context=None) -> bool:
    return (context or get_default_context()).ok()


def spin(node, executor=None):
    _executor.spin(node, executor)


def spin_once(node, timeout_sec=None, executor=None):
    _executor.spin_once(node, timeout_sec=timeout_sec, executor=executor)


def spin_until_future_complete(node, future, executor=None, timeout_sec=None):
    return _executor.spin_until_future_complete(
        node, future, executor=executor, timeout_sec=timeout_sec)


def remove_ros_args(args=None):
    """*args* without the ROS-specific tail.

    ``--ros-args`` and everything after it is rclpy's own namespace. The shim's
    equivalents are environment variables (``NAV2_SHM_NAMESPACE``,
    ``NAV2_SHM_GRAPH_DIR``, the reset). So the tail is dropped and the rest is
    handed back for the script's own argument parsing, which is what a script
    that calls this is asking for.
    """
    if args is None:
        args = sys.argv
    result = []
    for argument in args:
        if argument == "--ros-args":
            break
        result.append(argument)
    return result


def get_rmw_implementation_identifier():
    """There is no RMW here; the name says what the shim is instead."""
    return "ros2_shim"


__all__ = ["get_rmw_implementation_identifier", "ok", "remove_ros_args",
           "spin", "spin_once", "spin_until_future_complete"]
