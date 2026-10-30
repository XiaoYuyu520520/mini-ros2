"""The loop that polls every node, and the two functions that wrap it.

rclpy's executor is what lets a script read as straight-line code while nothing
underneath it is: ``spin`` keeps asking each node for whatever has arrived and
then runs the callbacks those messages produced, and
``spin_until_future_complete`` is that same loop with a stop condition.

Two things about this one are deliberate.

**One thread.** Everything the scripts do - set a flag in a feedback callback
and read it from a subscription, chain ``goal_response_callback`` into
``get_result_async`` - assumes callbacks run in a defined order on one thread,
which is what ``rclpy.spin`` on a ``SingleThreadedExecutor`` gives them. There
is no lock here and no callback group scheduling, because there is no second
thread to have a race with. ``MultiThreadedExecutor`` exists as a name so that
a script which asks for one imports, and it runs this same single loop; a
callback that blocks would have starved a real one too, since nav2's mission
scripts never hold a lock while waiting.

**Done callbacks run here, not where the future was resolved.** That is
``futures.run_pending`` and the reason it exists: `poll()` walks the rings, and
a callback that ran inline inside it would re-enter the ring that is being
drained. rclpy has the same rule - a done callback runs on the executor, never
inside whatever set the result.
"""
from __future__ import annotations

import time

from . import futures
from .node import nodes

# How long to wait when a pass found nothing at all. A shared-memory ring
# cannot be waited on, so "nothing arrived" has to become a short sleep or a
# mission script burns a core doing nothing; a millisecond is what the C++
# side's own wait loops use, and it is three orders of magnitude finer than the
# 50 Hz the topics actually run at.
IDLE_SLEEP = 0.001


class Executor:
    """The nodes to poll, and for how long."""

    def __init__(self, context=None):
        self._nodes = []
        self._shutdown = False

    def add_node(self, node) -> None:
        for existing in self._nodes:
            if existing is node:
                return
        self._nodes.append(node)

    def remove_node(self, node) -> None:
        self._nodes = [existing for existing in self._nodes if existing is not node]

    def shutdown(self, timeout_sec=None) -> None:
        self._shutdown = True

    def wake(self) -> None:
        """Nothing to wake - there is no blocking wait to interrupt."""

    def spin_once(self, timeout_sec=None) -> None:
        self._poll(timeout_sec)

    def spin(self) -> None:
        while not self._shutdown:
            self._poll(None)

    # ---------------------------------------------------------------- inside
    def _poll(self, timeout_sec=None) -> int:
        handled = 0
        for node in list(self._nodes):
            handled += node.poll()
        # The callbacks of everything that was resolved during that walk, in
        # the order it was resolved. See the module docstring.
        handled += futures.run_pending()
        if handled == 0:
            wait = IDLE_SLEEP if timeout_sec is None else min(IDLE_SLEEP, max(0.0, timeout_sec))
            if wait > 0.0:
                time.sleep(wait)
        return handled

    def _every_live_node(self) -> None:
        """Adopt the nodes of this process that nobody has handed to us.

        `rclpy.spin(node)` takes one node, but a script that builds a second
        one - or a helper library that does - expects it to be spun too, and
        `rclpy` gets that from the node's own context rather than from the
        caller. Added once, at the start of a spin.
        """
        for node in nodes():
            self.add_node(node)


class SingleThreadedExecutor(Executor):
    """The one the scripts get."""


class MultiThreadedExecutor(Executor):
    """The name a script may ask for; the loop is the single-threaded one.

    See the module docstring. Documented rather than absent so that the import
    in a ported script resolves and the behaviour is the defined one instead of
    a surprise.
    """

    def __init__(self, num_threads=None, context=None):
        super().__init__(context)
        self._num_threads = num_threads


def get_global_executor() -> Executor:
    """The executor `rclpy.spin` uses when the caller does not pass one."""
    global _GLOBAL
    if _GLOBAL is None:
        _GLOBAL = SingleThreadedExecutor()
    return _GLOBAL


_GLOBAL = None


def spin(node, executor=None) -> None:
    """Serve one node's callbacks until the process is shut down."""
    executor = executor or get_global_executor()
    executor.add_node(node)
    executor._every_live_node()
    try:
        executor.spin()
    finally:
        executor.remove_node(node)


def spin_once(node, timeout_sec=None, executor=None) -> None:
    executor = executor or get_global_executor()
    executor.add_node(node)
    try:
        executor.spin_once(timeout_sec=timeout_sec)
    finally:
        executor.remove_node(node)


def spin_until_future_complete(node, future, executor=None, timeout_sec=None):
    """Poll *node* until *future* is done, and return its result.

    ``None`` on timeout, with the future left as it was - the caller can tell
    the two apart by asking the future afterwards, which is how a behavior tree
    distinguishes "the navigator answered" from "the navigator went quiet".
    """
    executor = executor or get_global_executor()
    executor.add_node(node)
    try:
        deadline = None if timeout_sec is None else time.monotonic() + float(timeout_sec)
        while not future.done():
            executor.spin_once(timeout_sec=0.0)
            if deadline is not None and time.monotonic() >= deadline:
                break
        # One last pass so the callbacks chained off this future - the usual
        # `goal_response_callback` reaching for `get_result_async` - have run
        # before the caller looks at the result.
        futures.run_pending()
    finally:
        executor.remove_node(node)
    if not future.done():
        return None
    return future.result()
