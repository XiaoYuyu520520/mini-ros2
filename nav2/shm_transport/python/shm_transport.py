"""ctypes binding for the nav2 shared-memory transport.

Loads the `shm_transport` shared library produced by the CMake build and exposes
a small Pythonic wrapper used by the Python interop tests.
"""

import ctypes
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))


def _find_library():
    names = ["shm_transport.dll", "libshm_transport.dll", "libshm_transport.so",
             "libshm_transport.dylib", "shm_transport.dylib"]
    roots = [
        os.path.join(_HERE, "..", "build"),
        os.path.join(_HERE, "..", "..", "shm_transport", "build"),
        os.path.join(_HERE, "..", "..", "ros2_shim", "build"),
        os.path.join(_HERE, "..", "..", "..", "build"),
    ]
    for root in roots:
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in names:
                if name in filenames:
                    return os.path.join(dirpath, name)
    if os.environ.get("SHM_TRANSPORT_LIB"):
        return os.environ["SHM_TRANSPORT_LIB"]
    raise OSError("shm_transport shared library not found; build the project first")


_lib = ctypes.CDLL(_find_library())

_lib.shm_channel_create.restype = ctypes.c_void_p
_lib.shm_channel_create.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
_lib.shm_channel_open.restype = ctypes.c_void_p
_lib.shm_channel_open.argtypes = [ctypes.c_char_p]
_lib.shm_channel_publish.restype = ctypes.c_int
_lib.shm_channel_publish.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
_lib.shm_channel_subscribe.restype = ctypes.c_int
_lib.shm_channel_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                               ctypes.POINTER(ctypes.c_size_t), ctypes.c_int]
_lib.shm_channel_published_count.restype = ctypes.c_uint64
_lib.shm_channel_published_count.argtypes = [ctypes.c_void_p]
_lib.shm_channel_read_count.restype = ctypes.c_uint64
_lib.shm_channel_read_count.argtypes = [ctypes.c_void_p]
_lib.shm_channel_dropped_count.restype = ctypes.c_uint64
_lib.shm_channel_dropped_count.argtypes = [ctypes.c_void_p]
_lib.shm_channel_close.restype = None
_lib.shm_channel_close.argtypes = [ctypes.c_void_p]


class ShmChannel:
    """One endpoint of a named broadcast channel (writer or reader)."""

    def __init__(self, handle):
        # Set the attribute before the check: a failed open raises, and __del__
        # then runs on a half-built object, where a missing _handle turns one
        # clear RuntimeError into a second, confusing AttributeError.
        self._handle = None
        if not handle:
            raise RuntimeError("failed to open shared-memory channel")
        self._handle = handle

    @classmethod
    def create(cls, name, slot_count, slot_size):
        handle = _lib.shm_channel_create(name.encode("utf-8"), slot_count, slot_size)
        return cls(handle)

    @classmethod
    def open(cls, name):
        handle = _lib.shm_channel_open(name.encode("utf-8"))
        return cls(handle)

    def publish(self, payload):
        data = bytes(payload)
        buf = ctypes.create_string_buffer(data, len(data))
        return _lib.shm_channel_publish(self._handle, buf, len(data)) == 0

    def subscribe(self, capacity=1 << 20, timeout_ms=-1):
        out = ctypes.create_string_buffer(capacity)
        out_size = ctypes.c_size_t(0)
        rc = _lib.shm_channel_subscribe(self._handle, out, capacity,
                                ctypes.byref(out_size), timeout_ms)
        if rc != 0:
            return None
        return out.raw[:out_size.value]

    @property
    def published_count(self):
        return _lib.shm_channel_published_count(self._handle)

    @property
    def read_count(self):
        return _lib.shm_channel_read_count(self._handle)

    @property
    def dropped_count(self):
        return _lib.shm_channel_dropped_count(self._handle)

    def close(self):
        if self._handle:
            _lib.shm_channel_close(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.close()

    def __del__(self):
        self.close()


def path_parts():
    return _lib, sys.platform
