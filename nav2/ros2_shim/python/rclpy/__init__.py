"""``rclpy``, for a script that was written against ROS2.

This package is the seam that lets a ROS2 Python script run on the shim
unchanged. The racecar mission scripts were written against rclpy, and not one
of them should have to know that discovery here is a directory of text files
and that delivery is a ring of shared-memory slots - so ``import rclpy``,
``rclpy.init()``, ``Node``, ``create_publisher``, ``create_subscription``,
``ActionClient``, ``rclpy.spin`` all keep their names and their meanings.

It is deliberately thin. Everything with behaviour is in :mod:`ros2py`, one
import below this one, and each module here is either a re-export of it or the
few lines where rclpy spells the same thing differently. Reading a file here
should tell you where the implementation of that name actually lives.

Importing this also makes the ROS2 message names importable. On a real ROS2
``geometry_msgs`` is an installed package; here it is the generated tree under
``pymessages/``, and :func:`ros2py.aliases.install` is what maps one spelling
onto the other. Every one of these scripts imports ``rclpy`` before it names a
message type, which is what makes this the right place for that to happen.
"""
from __future__ import annotations

import sys as _sys

from ros2py.aliases import install as _install_message_aliases

_install_message_aliases()

from . import action, callback_groups, client, clock, context, duration  # noqa: E402,F401
from . import exceptions, executors, logging, node, parameter, publisher  # noqa: E402,F401
from . import qos, signals, subscription, task, time, timer, utilities  # noqa: E402,F401
from .context import Context, get_default_context  # noqa: E402
from .node import Node  # noqa: E402
from .parameter import Parameter  # noqa: E402
from .qos import QoSProfile  # noqa: E402
from .signals import SignalHandlerOptions  # noqa: E402
from .utilities import remove_ros_args  # noqa: E402

__all__ = [
    "Context",
    "Node",
    "Parameter",
    "QoSProfile",
    "SignalHandlerOptions",
    "action",
    "callback_groups",
    "client",
    "clock",
    "context",
    "create_node",
    "destroy_node",
    "duration",
    "exceptions",
    "executors",
    "init",
    "logging",
    "node",
    "ok",
    "parameter",
    "publisher",
    "qos",
    "remove_ros_args",
    "shutdown",
    "signals",
    "spin",
    "spin_once",
    "spin_until_future_complete",
    "subscription",
    "task",
    "time",
    "timer",
    "try_on_shutdown",
    "try_shutdown",
    "utilities",
]


def init(args=None, context=None, domain_id=None, signal_handler_options=None,
         automatically_declare_parameters_from_overrides=False):
    """Bring the library up, and answer with the context it is up in.

    *domain_id* is taken and ignored: there is no DDS domain to join, and a
    script that passes one is asking to be isolated from other robots, which on
    the shim is ``NAV2_SHM_NAMESPACE`` instead.

    *args* - ``sys.argv`` when the caller passes none, which is what a script
    written as a bare ``rclpy.init()`` expects - is read for its ``--ros-args``
    tail, and ``-p name:=value`` there becomes a parameter override that every
    node created after this point starts with. ``use_sim_time`` is what that is
    for here: a nav2 node on this shim runs on simulation time (``nav.yaml``
    sets it for every one of them), so a mission script that compares a stamp it
    read off the wire against its own clock has to be on the same timeline, and
    ``--ros-args -p use_sim_time:=true`` is how it gets there.

    ``ROS2_SHIM_SIM_TIME`` is the same thing for a caller that cannot rely on
    the command line surviving the script: a script that filters its own argv
    before calling this (``go.py`` keeps only the dash-prefixed arguments) drops
    a ``-p`` whose value is not one. It is read only for ``use_sim_time``, and
    only where the command line did not already answer, so the two cannot fight.
    """
    from ros2py import node as _node_module

    overrides = _node_module.overrides_from_args(_sys.argv if args is None else args)
    if "use_sim_time" not in overrides:
        from_environment = _node_module.sim_time_from_environment()
        if from_environment is not None:
            overrides["use_sim_time"] = from_environment
    _node_module.set_parameter_overrides(overrides)
    context = context or get_default_context()
    context.init(args)
    signals.install_signal_handlers(signal_handler_options)
    return context


def shutdown(context=None):
    """Take it down. Idempotent, so a script may call it in a ``finally``."""
    executors.get_global_executor().shutdown()
    (context or get_default_context()).shutdown()


def try_shutdown(context=None):
    (context or get_default_context()).try_shutdown()


def ok(context=None) -> bool:
    return (context or get_default_context()).ok()


def spin(node, executor=None):
    """Serve one node's callbacks until the process is shut down.

    Ctrl-C counts as shutting it down: ``init()`` installs the handler, so the
    call returns and the script's own cleanup runs. See :mod:`rclpy.signals`.
    """
    executors.spin(node, executor)


def spin_once(node, timeout_sec=None, executor=None):
    executors.spin_once(node, timeout_sec=timeout_sec, executor=executor)


def spin_until_future_complete(node, future, executor=None, timeout_sec=None):
    return executors.spin_until_future_complete(
        node, future, executor=executor, timeout_sec=timeout_sec)


def create_node(node_name, namespace=None, context=None, cli_args=None, **kwargs):
    return Node(node_name, namespace=namespace, context=context,
                cli_args=cli_args, **kwargs)


def destroy_node(node):
    node.destroy_node()


def try_on_shutdown(callback, context=None):
    """Run *callback* when the context goes down; usable as a decorator."""
    (context or get_default_context()).add_on_shutdown_callback(callback)
    return callback


def get_node_names(context=None, node=None):
    """Every node the graph knows about, this process' included."""
    from ros2py import graph

    return graph.read().node_names()
