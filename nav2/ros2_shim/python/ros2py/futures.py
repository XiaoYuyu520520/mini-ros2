"""The small promise an async call hands back, and who runs its callbacks.

``send_goal_async`` returns one of these, a script does
``future.add_done_callback(...)`` on it, and the callback runs when the answer
arrives. *When* it runs is the part that has to be right: rclpy runs done
callbacks on the executor's thread after the callback that resolved the future
has returned, never inline at the moment of resolution. The racecar scripts
depend on that - they set state in one callback and read it from another - and
inline dispatch would re-enter the poll loop that is still draining the ring.

So resolving a future only marks it; :func:`run_pending` is what calls the
callbacks, and the executor calls that once per spin.
"""
from __future__ import annotations


class Future:
    """rclpy's Future: a result that arrives later, plus the callbacks for it."""

    def __init__(self):
        self._done = False
        self._result = None
        self._exception = None
        self._callbacks = []

    def done(self) -> bool:
        return self._done

    def cancelled(self) -> bool:
        return False

    def cancel(self) -> bool:
        return False

    def result(self):
        """The value, or raise what went wrong.

        rclpy raises here rather than returning None, and a script that writes
        ``future.result().result`` needs the exception to surface at that line
        instead of as an AttributeError three frames away.
        """
        if self._exception is not None:
            raise self._exception
        return self._result

    def exception(self):
        return self._exception

    def add_done_callback(self, callback) -> None:
        if self._done:
            _queue(self)
        else:
            self._callbacks.append(callback)

    def remove_done_callback(self, callback) -> None:
        try:
            self._callbacks.remove(callback)
        except ValueError:
            pass

    # ------------------------------------------------------- resolving it
    def set_result(self, value) -> None:
        if self._done:
            return
        self._result = value
        self._done = True
        _queue(self)

    def set_exception(self, exception) -> None:
        if self._done:
            return
        self._exception = exception
        self._done = True
        _queue(self)


# rclpy spells the same thing `Task` where it is a coroutine-backed one.
Task = Future


_PENDING = []


def _queue(future: Future) -> None:
    if future not in _PENDING:
        _PENDING.append(future)


def run_pending() -> int:
    """Call the done callbacks of everything resolved since the last call."""
    ran = 0
    while _PENDING:
        future = _PENDING.pop(0)
        for callback in list(future._callbacks):
            callback(future)
            ran += 1
    return ran


def has_pending() -> bool:
    return bool(_PENDING)
