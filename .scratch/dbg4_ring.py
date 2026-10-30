# -*- coding: utf-8 -*-
"""Fourth probe: does a Python publish land in the ring at all?"""
import ctypes
import os

from ros2py import channel

lib = channel._library()
name = channel.channel_name("/navigate_to_pose/_goal").encode("utf-8")
print("DBG name=%r" % (name,), flush=True)
h = lib.shm_channel_create(name, 8, 1 << 21)
print("DBG create handle=%r" % (h,), flush=True)
print("DBG published before=%d read=%d" % (
    lib.shm_channel_published_count(h), lib.shm_channel_read_count(h)), flush=True)
payload = b"\x01" + bytes(7) + b"python-probe-payload"
buf = ctypes.create_string_buffer(payload, len(payload))
rc = lib.shm_channel_publish(h, buf, len(payload))
print("DBG publish rc=%d published after=%d" % (
    rc, lib.shm_channel_published_count(h)), flush=True)
h2 = lib.shm_channel_open(name)
print("DBG second open handle=%r published=%d" % (
    h2, lib.shm_channel_published_count(h2) if h2 else -1), flush=True)
if h2:
    out = ctypes.create_string_buffer(1 << 21)
    size = ctypes.c_size_t(0)
    rc2 = lib.shm_channel_subscribe(h2, out, 1 << 21, ctypes.byref(size), 100)
    print("DBG subscribe rc=%d size=%d tail=%r" % (
        rc2, size.value, out.raw[max(0, size.value - 16):size.value]), flush=True)
    lib.shm_channel_close(h2)
lib.shm_channel_close(h)
