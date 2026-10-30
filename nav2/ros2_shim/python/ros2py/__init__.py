"""The shim\'s Python surface: enough of ROS2 for a Python node to join the stack.

A shim stack is a set of processes talking over named shared-memory channels,
with one text file per process in a directory as the only discovery there is.
Everything on this side of it - the ported nav2 servers, the gRPC daemon, the
``ros2`` CLI - goes through those two things, and so does this package:

* :mod:`ros2py.graph` - the registry: which nodes/topics/services/actions exist
* :mod:`ros2py.channel` - the naming and sizing rules for the channels behind them
* :mod:`ros2py.node` - a node: publishers, subscriptions, timers, clients
* :mod:`ros2py.action` - action clients over the six channels of an action
* :mod:`ros2py.executor` - the loop that polls all of the above
* :mod:`ros2py.aliases` - ``geometry_msgs`` and friends, spelled as ROS2 spells them

``rclpy`` next to this package is the thin facade that makes a ROS2 Python
script run on the shim unchanged; this package is where the work is.
"""
from __future__ import annotations

from .aliases import install as install_message_aliases

__all__ = ["install_message_aliases"]
