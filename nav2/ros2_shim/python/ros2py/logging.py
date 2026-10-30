"""`get_logger()` and the one thing a node does with it: say something.

The format is rclpy's default, because these scripts are read next to the real
car's output: ``[INFO] [<seconds>.<nanoseconds>] [<node>]: <message>``, one
line on stderr. Nothing here publishes ``/rosout`` - a Python node's log does
not need to cross the bridge, and the daemon's own log rotation is about
topics, not about this.
"""
from __future__ import annotations

import sys
import time

SEVERITIES = {"DEBUG": 10, "INFO": 20, "WARN": 30, "ERROR": 40, "FATAL": 50}

_LEVEL_THRESHOLD = SEVERITIES["INFO"]


def set_severity(level) -> None:
    """``DEBUG`` and above, exactly as rclpy's ``set_logger_level`` spells it."""
    global _LEVEL_THRESHOLD
    if isinstance(level, str):
        _LEVEL_THRESHOLD = SEVERITIES.get(level.upper(), SEVERITIES["INFO"])
    else:
        _LEVEL_THRESHOLD = int(level)


class Logger:
    """The object ``Node.get_logger()`` returns."""

    def __init__(self, name: str):
        self.name = name

    def _log(self, severity: str, message: str) -> None:
        if SEVERITIES[severity] < _LEVEL_THRESHOLD:
            return
        now = time.time()
        nanoseconds = int(now * 1e9)
        sys.stderr.write("[%s] [%d.%09d] [%s]: %s\n" % (
            severity, nanoseconds // 1000000000, nanoseconds % 1000000000,
            self.name.lstrip("/"), message))
        sys.stderr.flush()

    def debug(self, message: str) -> None:
        self._log("DEBUG", message)

    def info(self, message: str) -> None:
        self._log("INFO", message)

    def warn(self, message: str) -> None:
        self._log("WARN", message)

    def warning(self, message: str) -> None:
        self._log("WARN", message)

    def error(self, message: str) -> None:
        self._log("ERROR", message)

    def fatal(self, message: str) -> None:
        self._log("FATAL", message)

    def get_name(self) -> str:
        return self.name


def get_logger(name: str) -> Logger:
    return Logger(name)
