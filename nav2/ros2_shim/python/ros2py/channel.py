"""What a topic is called in shared memory, and how big its channel is.

Both ends of a channel have to agree on two things, and ``shm_config.hpp`` on
the C++ side is where they are decided:

* the **name**: ``ros2shim`` + ``NAV2_SHM_NAMESPACE`` + the resolved topic name.
  One flat namespace, which is what makes the daemon's mirror of ``/scan`` and
  a nav2 node's subscription line up with no configuration at all.
* the **slot size**: a grid (``/map``, anything with ``costmap`` in it) needs a
  large slot, a parameter-shaped name a small one, everything else the default.
  A publisher that picked its own size would still be read by anyone, but it
  would commit the wrong amount of pagefile for a map - and a *subscriber* that
  opened with the wrong capacity silently drops messages, because `subscribe`
  advances the cursor and reports failure when the buffer is too small.

This module is the Python spelling of those rules, plus the endpoint a node
polls. It talks to the transport through its C entry points rather than the
``ShmChannel`` wrapper next door, for one reason that matters at a node's
poll rate: ``subscribe`` takes a buffer, and the wrapper allocates a fresh one
per call. A two-megabyte allocation and zero-fill per subscription per poll is
most of a core before any message has arrived, so the buffer here is allocated
once, at the channel's own slot size, and reused.
"""
from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _shm_transport_module():
    """The ``ctypes`` binding for the shared-memory transport.

    It lives next to the transport's own Python directory
    (``nav2/shm_transport/python``), and a script run from anywhere should be
    able to use this module without being told where that is - so the
    directory is added here, once, rather than pushed onto every caller.
    """
    try:
        import shm_transport
        return shm_transport
    except ImportError:
        pass
    candidate = _HERE.parent.parent.parent / "shm_transport" / "python"
    if str(candidate) not in sys.path:
        sys.path.insert(0, str(candidate))
    import shm_transport
    return shm_transport


def _library():
    return _shm_transport_module().path_parts()[0]


def namespace() -> str:
    return os.environ.get("NAV2_SHM_NAMESPACE", "")


def channel_name(topic: str) -> str:
    """``/scan`` -> ``ros2shim/scan`` (``ros2shimtest/scan`` with a namespace)."""
    return "ros2shim" + namespace() + topic


def resolve_name(name: str, node_namespace: str = "/") -> str:
    """A topic name the way the graph records it: absolute, one leading slash.

    ``rcl`` resolves a relative name against the node's namespace and the
    shim's ``Node::resolve_topic`` does the same; the graph record is compared
    by string, so the spelling has to match. ``/`` and ``foo`` are the same
    name for a node in the root namespace, which is the only kind these scripts
    build.
    """
    if not name:
        return "/"
    if name.startswith("/"):
        return name if len(name) == 1 else name.rstrip("/")
    base = node_namespace.rstrip("/")
    return (base + "/" + name) if base else "/" + name


def env_u64(name: str, fallback: int) -> int:
    value = os.environ.get(name, "")
    if not value:
        return fallback
    try:
        return int(value)
    except ValueError:
        return fallback


def default_slot_count() -> int:
    return env_u64("NAV2_SHM_SLOT_COUNT", 8)


def default_slot_size() -> int:
    return env_u64("NAV2_SHM_SLOT_SIZE", 2 << 20)


def large_slot_size() -> int:
    return env_u64("NAV2_SHM_LARGE_SLOT_SIZE", 8 << 20)


def parameter_slot_size() -> int:
    return env_u64("NAV2_SHM_PARAM_SLOT_SIZE", 256 << 10)


_PARAMETER_SERVICES = (
    "/get_parameters", "/get_parameter_types", "/set_parameters",
    "/set_parameters_atomically", "/describe_parameters", "/list_parameters",
)


def is_parameter_service_name(name: str) -> bool:
    return name.endswith(_PARAMETER_SERVICES)


def slot_size_for_topic(topic: str) -> int:
    """The slot size the C++ side would create this channel with.

    The order is the C++ order: a parameter service first, then the two other
    parameter-shaped names, then ``...info`` (which has ``costmap`` in its
    address but is a handful of numbers), and only then the grids.
    """
    if is_parameter_service_name(topic):
        return parameter_slot_size()
    if topic in ("/parameter_events", "/rosout"):
        return parameter_slot_size()
    # `<action>/_action/status`: a GoalStatusArray per action server, so the
    # same rule and the same size `shm_config.hpp` gives it.
    if topic.endswith("/_action/status"):
        return parameter_slot_size()
    if "info" in topic:
        return default_slot_size()
    if "costmap" in topic or "map" in topic or "voxel" in topic:
        return large_slot_size()
    return default_slot_size()


class Channel:
    """One endpoint of one named channel.

    A channel has **one writer**. That is not a convention but the transport's
    design - a ring with one publish cursor - and it is why a node that wants
    to reshape a topic has to be its only publisher rather than a second one
    next to the original (see the `-r cmd_vel:=cmd_vel_nav` remaps the safety
    chain uses for exactly this).
    """

    def __init__(self, handle, name: str, writer: bool, capacity: int):
        self._handle = handle
        self.name = name
        self.writer = writer
        self._capacity = capacity
        self._buffer = ctypes.create_string_buffer(capacity)
        self._size = ctypes.c_size_t(0)

    # ------------------------------------------------------------- opening
    @classmethod
    def open(cls, topic: str):
        """Open an existing channel, or None when nobody has created it yet."""
        handle = _library().shm_channel_open(channel_name(topic).encode("utf-8"))
        if not handle:
            return None
        return cls(handle, channel_name(topic), False, slot_size_for_topic(topic))

    @classmethod
    def create(cls, topic: str):
        """Create the channel, adopting an existing one if there is one.

        ``shm_channel_create`` is the shim's open-or-create on purpose: a
        publisher that starts before its subscriber and one that starts after
        both have to end up on the one channel, and a channel left behind by
        processes that were killed is re-initialised rather than inherited.
        """
        size = slot_size_for_topic(topic)
        handle = _library().shm_channel_create(
            channel_name(topic).encode("utf-8"), default_slot_count(), size)
        if not handle:
            return None
        return cls(handle, channel_name(topic), True, size)

    @classmethod
    def open_or_create(cls, topic: str):
        return cls.create(topic)

    # --------------------------------------------------------------- using
    def publish(self, payload) -> bool:
        data = bytes(payload)
        if len(data) > self._capacity:
            # The C++ side reports the same thing the same way: the payload is
            # larger than the slot, so nothing was published.
            return False
        buffer = ctypes.create_string_buffer(data, len(data))
        return _library().shm_channel_publish(self._handle, buffer, len(data)) == 0

    def poll(self):
        """Whatever is in the ring right now, or None. Never blocks."""
        return self.receive(0)

    def receive(self, timeout_ms: int = 0):
        """The next message, or None after waiting up to *timeout_ms*."""
        size = self._receive_into_buffer(timeout_ms)
        if size < 0:
            return None
        # `string_at` rather than `buffer.raw`: `.raw` materialises the whole
        # slot before the slice is taken, so every received message copies the
        # full 2 MB (8 MB on a costmap) and throws all but a few hundred bytes
        # away. At `odom`'s 50 Hz that is a quarter of a core spent on memcpy.
        return ctypes.string_at(self._buffer, size)

    def _receive_into_buffer(self, timeout_ms: int = 0) -> int:
        """Read the next message into the reusable buffer; its size, or -1."""
        self._size.value = 0
        result = _library().shm_channel_subscribe(
            self._handle, self._buffer, self._capacity, ctypes.byref(self._size),
            int(timeout_ms))
        if result != 0:
            return -1
        return self._size.value

    def drain(self) -> int:
        """Throw away what is already in the ring, and say how much there was.

        What the C++ client calls ``seek_to_latest()`` on a new channel: the
        ring keeps its contents after its writer goes away, and a subscriber
        that has just joined must not be fed a previous run's leftovers.
        ROS2's volatile durability drops everything published before the
        subscription existed, and so does this.
        """
        skipped = 0
        while self._receive_into_buffer(0) >= 0:
            skipped += 1
        return skipped

    def close(self) -> None:
        if self._handle:
            _library().shm_channel_close(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_exception):
        self.close()
