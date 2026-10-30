"""The exceptions rclpy raises, so that an ``except`` clause resolves."""
from __future__ import annotations


class RCLPYError(Exception):
    """Base of everything rclpy raises on purpose."""


class NotInitializedException(RCLPYError):
    """Used by nothing here: the shim does not require ``init()`` first."""


class AlreadyInitializedException(RCLPYError):
    pass


class InvalidHandle(RCLPYError):
    pass


class InvalidNamespaceException(RCLPYError, ValueError):
    pass


class InvalidNodeNameException(RCLPYError, ValueError):
    pass


class InvalidTopicNameException(RCLPYError, ValueError):
    pass


class InvalidServiceNameException(RCLPYError, ValueError):
    pass


class InvalidActionNameException(RCLPYError, ValueError):
    pass


class ParameterException(RCLPYError):
    pass


class ParameterNotDeclaredException(ParameterException):
    pass


class ParameterAlreadyDeclaredException(ParameterException):
    pass


class ParameterUninitializedException(ParameterException):
    pass


class InvalidParametersException(ParameterException):
    pass


class ShutdownException(RCLPYError):
    """Raised by rclpy's executor to unwind a spin from another thread."""


class ExternalShutdownException(ShutdownException):
    """The signal handler's way out of a blocking spin."""


class RCLError(RCLPYError):
    pass
