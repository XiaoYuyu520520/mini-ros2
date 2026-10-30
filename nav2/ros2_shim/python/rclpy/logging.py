"""Logging, under the names rclpy exposes at module level.

``get_logger`` and the severity spelling are rclpy's; the formatting is in
:mod:`ros2py.logging` and is rclpy's default, because these scripts are read
next to the real car's output.
"""
from enum import IntEnum

from ros2py.logging import Logger, SEVERITIES, get_logger, set_severity  # noqa: F401


class LoggingSeverity(IntEnum):
    """rclpy's severity enum, numbered the way rclpy numbers it."""

    UNSET = 0
    DEBUG = 10
    INFO = 20
    WARN = 30
    ERROR = 40
    FATAL = 50


def set_logger_level(name, level, logger_name=None) -> None:
    """Set one logger's threshold. The shim has one threshold per process."""
    set_severity(level)


def get_logging_directory():
    return None


def get_logger_level(logger_name=None):
    return None


__all__ = ["Logger", "LoggingSeverity", "SEVERITIES", "get_logger",
           "get_logger_level", "get_logging_directory", "set_logger_level",
           "set_severity"]
