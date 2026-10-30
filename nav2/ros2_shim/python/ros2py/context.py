"""The context a node is created in: on the shim, the process' own lifetime.

rclpy's context owns the DDS participant, the signal handlers and the logging
configuration. There is no participant to own here - discovery is a directory
of text files and the transport is a handle per channel - so this carries the
two things that really are process-wide and that callers really do ask about:
whether the library is up, and who wants to hear about it going down.

It is a real object rather than a stub because ``rclpy.init()`` returns one and
a script may hand it to ``Node(...)``, which the shim accepts and ignores.
"""
from __future__ import annotations


class Context:
    """Up or down, plus the callbacks for the transition."""

    def __init__(self, args=None):
        self._args = args
        self._ok = False
        self._shutdown_callbacks = []

    def init(self, args=None) -> bool:
        if args is not None:
            self._args = args
        self._ok = True
        return True

    def ok(self) -> bool:
        return self._ok

    def shutdown(self) -> None:
        """Idempotent, so a script may call it from a ``finally`` block."""
        if not self._ok:
            return
        self._ok = False
        for callback in list(self._shutdown_callbacks):
            callback()
        self._shutdown_callbacks.clear()

    def try_shutdown(self) -> None:
        if self._ok:
            self.shutdown()

    def add_on_shutdown_callback(self, callback) -> None:
        self._shutdown_callbacks.append(callback)

    def on_shutdown(self, callback):
        self.add_on_shutdown_callback(callback)
        return callback

    @property
    def args(self):
        return self._args

    def __repr__(self):
        return "Context(ok=%s)" % self._ok


_DEFAULT = []


def get_default_context() -> Context:
    """The one ``rclpy.init()`` brings up when the caller passes none."""
    if not _DEFAULT:
        _DEFAULT.append(Context())
    return _DEFAULT[0]
