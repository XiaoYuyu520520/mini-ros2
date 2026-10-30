"""Ctrl-C, made to stop the stack the way rclpy makes it stop.

rclpy's ``init()`` installs SIGINT/SIGTERM handlers that shut the context down,
so that a spinning script *returns* instead of having a KeyboardInterrupt
raised at whatever line the interpreter happened to be on, and its own cleanup
- ``destroy_node()``, ``shutdown()`` - runs on the way out. That difference is
worth keeping: a mission script that is interrupted should still give its
channels and its graph record back.

``SignalHandlerOptions.NONE`` opts out, and a thread that is not the main thread
cannot install handlers at all, so it does not try.
"""
from __future__ import annotations

import signal
import threading
from enum import Enum

from ros2py import context as _context
from ros2py import executor as _executor


class SignalHandlerOptions(Enum):
    ALL = 0
    SIGINT = 1
    SIGTERM = 2
    NONE = 3


def _shutdown_on_signal(signum, frame) -> None:
    _executor.get_global_executor().shutdown()
    _context.get_default_context().shutdown()


def install_signal_handlers(options=None) -> bool:
    """True when handlers were installed; see the module docstring."""
    if options is None:
        options = SignalHandlerOptions.ALL
    if options == SignalHandlerOptions.NONE:
        return False
    if threading.current_thread() is not threading.main_thread():
        return False
    wanted = []
    if options in (SignalHandlerOptions.ALL, SignalHandlerOptions.SIGINT):
        wanted.append(signal.SIGINT)
    if options in (SignalHandlerOptions.ALL, SignalHandlerOptions.SIGTERM):
        wanted.append(signal.SIGTERM)
    for number in wanted:
        signal.signal(number, _shutdown_on_signal)
    return bool(wanted)
