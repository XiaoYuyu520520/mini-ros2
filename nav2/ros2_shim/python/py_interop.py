#!/usr/bin/env python3
"""Python side of the cross-language interoperability harness.

Mirrors ros2_shim/tests/py_interop.cpp byte for byte.
"""

import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_HERE, "..", "..", "shm_transport", "python"))

from pymessages.nav_msgs.msg.path import Path  # noqa: E402
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped  # noqa: E402
from shm_transport import ShmChannel  # noqa: E402


def pose_at(i):
    p = PoseStamped()
    p.header.frame_id = "map"
    p.header.stamp.sec = 5
    p.header.stamp.nanosec = 6
    p.pose.position.x = 1.5 * i
    p.pose.position.y = -2.5 * i
    p.pose.position.z = 0.25 * i
    p.pose.orientation.x = 0.1
    p.pose.orientation.y = 0.2
    p.pose.orientation.z = 0.3
    p.pose.orientation.w = 0.9
    return p


def canonical_path():
    path = Path()
    path.header.frame_id = "map"
    path.header.stamp.sec = 5
    path.header.stamp.nanosec = 6
    for i in range(4):
        path.poses.append(pose_at(i))
    return path


def verify_canonical(path):
    if path.header.frame_id != "map" or path.header.stamp.sec != 5:
        return False
    if path.header.stamp.nanosec != 6 or len(path.poses) != 4:
        return False
    for i, p in enumerate(path.poses):
        if p.pose.position.x != 1.5 * i or p.pose.position.y != -2.5 * i:
            return False
        if p.pose.position.z != 0.25 * i or p.pose.orientation.w != 0.9:
            return False
        if p.header.frame_id != "map":
            return False
    return True


def dump(file_path):
    data = canonical_path().serialize()
    with open(file_path, "wb") as fh:
        fh.write(data)
    print(f"py dumped {len(data)} bytes to {file_path}")
    return 0


def check(file_path):
    with open(file_path, "rb") as fh:
        data = fh.read()
    if not data:
        print(f"py check: empty file {file_path}")
        return 2
    try:
        msg = Path.from_bytes(data)
    except Exception as exc:  # noqa: BLE001
        print(f"py check: deserialize failed: {exc}")
        return 3
    if not verify_canonical(msg):
        print("py check: content mismatch")
        return 4
    print(f"py check: OK ({len(data)} bytes, {len(msg.poses)} poses)")
    return 0


def pub(channel, slot_size):
    ch = ShmChannel.create(channel, 16, slot_size)
    data = canonical_path().serialize()
    sent = 0
    deadline = time.time() + 3.0
    while time.time() < deadline:
        if ch.publish(data):
            sent += 1
        time.sleep(0.05)
    print(f"py pub: sent {sent} messages")
    return 0 if sent else 5


def sub(channel, slot_size, timeout_ms):
    ch = ShmChannel.open(channel)
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        data = ch.subscribe(capacity=slot_size, timeout_ms=500)
        if data:
            try:
                msg = Path.from_bytes(data)
            except Exception as exc:  # noqa: BLE001
                print(f"py sub: deserialize failed: {exc}")
                return 3
            if not verify_canonical(msg):
                print("py sub: content mismatch")
                return 4
            print(f"py sub: OK ({len(data)} bytes, {len(msg.poses)} poses)")
            return 0
    print("py sub: timeout")
    return 6


def main(argv):
    if len(argv) < 3:
        print("usage: py_interop.py <dump|check|pub|sub> <arg> [args]")
        return 1
    mode = argv[1]
    if mode == "dump":
        return dump(argv[2])
    if mode == "check":
        return check(argv[2])
    if mode == "pub":
        return pub(argv[2], int(argv[3]))
    if mode == "sub":
        return sub(argv[2], int(argv[3]), int(argv[4]))
    print(f"unknown mode {mode}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
