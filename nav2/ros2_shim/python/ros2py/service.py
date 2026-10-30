"""Service clients, over the two channels an invocation needs.

A service in the shim is what it is upstream: a request that carries an id and
a reply that carries the same id back. The two are separate channels - the
caller is the only writer of ``<service>/_request`` and the callee the only
writer of ``<service>/_response`` - and the id is the whole of the correlation,
which is why a client starts numbering at 1 and throws away whatever is already
in the response ring when it opens: an answer left there by an earlier client
would otherwise be read back as the reply to this client's first request.

"Is the service up?" is answered from the graph registry rather than by waiting
to see, and that matters more than it looks: a caller that cannot tell sends its
request into a ring nobody is reading and waits out its whole timeout.
"""
from __future__ import annotations

import struct
import time

from . import channel, graph
from .futures import Future


def _frame(correlation_id: int, payload: bytes) -> bytes:
    return struct.pack("<Q", correlation_id) + payload


def _correlation_id(data: bytes):
    if len(data) < 8:
        return None
    return struct.unpack_from("<Q", data, 0)[0]


def _body(data: bytes) -> bytes:
    return data[8:]


class Client:
    """``node.create_client(Trigger, 'stop')``."""

    def __init__(self, node, service_type, service_name, qos=None):
        self.node = node
        self.service_type = service_type
        self.service_name = node.resolve_name(service_name)
        self.type_name = "%s/%s/%s" % (
            getattr(service_type, "__module__", "").split(".")[-2:][0] or "",
            getattr(service_type, "__module__", "").split(".")[-2:][-1],
            service_type.__name__)
        self._request = channel.Channel.create(self.service_name + "/_request")
        self._response = channel.Channel.create(self.service_name + "/_response")
        if self._response is not None:
            self._response.drain()
        self._pending = {}
        self._counter = 0
        self._last_open = 0.0

    # ------------------------------------------------------------- readiness
    def service_is_ready(self) -> bool:
        return graph.service_exists(self.service_name)

    def wait_for_service(self, timeout_sec=None) -> bool:
        deadline = None if timeout_sec is None else time.monotonic() + float(timeout_sec)
        while True:
            if self.service_is_ready():
                return True
            if deadline is not None and time.monotonic() >= deadline:
                return False
            # The caller is a script that is about to send a request, so a
            # short sleep is the honest thing here: there is nothing else for
            # this thread to do until the answer, and spinning would only make
            # the graph registry hotter for everyone else.
            time.sleep(0.05)

    def server_is_ready(self) -> bool:
        return self.service_is_ready()

    # ---------------------------------------------------------------- calling
    def call_async(self, request) -> Future:
        future = Future()
        self._ensure_channels()
        if self._request is None:
            future.set_exception(RuntimeError(
                "no channel for %s" % (self.service_name + "/_request")))
            return future
        self._counter += 1
        correlation_id = self._counter
        if not self._request.publish(_frame(correlation_id, request.serialize())):
            future.set_exception(RuntimeError(
                "request for %s did not fit its channel" % self.service_name))
            return future
        self._pending[correlation_id] = future
        return future

    def call(self, request, timeout_sec=None):
        """The blocking form, for a script that has nothing else to do."""
        future = self.call_async(request)
        deadline = None if timeout_sec is None else time.monotonic() + float(timeout_sec)
        while not future.done():
            self.poll()
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError("%s did not answer" % self.service_name)
            time.sleep(0.001)
        return future.result()

    def remove_pending_request(self, future) -> None:
        for correlation_id, pending in list(self._pending.items()):
            if pending is future:
                del self._pending[correlation_id]

    # ------------------------------------------------------------------ poll
    def poll(self) -> int:
        self._ensure_channels()
        if self._response is None:
            return 0
        answered = 0
        while True:
            data = self._response.poll()
            if data is None:
                break
            correlation_id = _correlation_id(data)
            if correlation_id is None:
                continue
            future = self._pending.pop(correlation_id, None)
            if future is None:
                continue  # an answer to a request this client gave up on
            try:
                future.set_result(self.service_type.Response.from_bytes(_body(data)))
            except Exception as error:  # noqa: BLE001
                future.set_exception(error)
            answered += 1
        return answered

    def _ensure_channels(self) -> None:
        if self._request is not None and self._response is not None:
            return
        now = time.monotonic()
        if now - self._last_open < 0.2:
            return
        self._last_open = now
        if self._request is None:
            self._request = channel.Channel.create(self.service_name + "/_request")
        if self._response is None:
            self._response = channel.Channel.create(self.service_name + "/_response")
            if self._response is not None:
                self._response.drain()

    def destroy(self) -> None:
        for endpoint in (self._request, self._response):
            if endpoint is not None:
                endpoint.close()
        self._request = self._response = None
        self._pending.clear()
