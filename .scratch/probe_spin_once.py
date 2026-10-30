# -*- coding: utf-8 -*-
"""`rclpy.spin_once(node, timeout_sec=N)` on an idle node: how long does it take?

On rclpy it blocks up to N seconds. Here `ros2py/executor.py` sleeps
`min(IDLE_SLEEP, N)` with `IDLE_SLEEP = 0.001`, so a caller that asks for a
second gets a millisecond - and an upstream "wait for a message" loop turns
into a ~700 Hz loop. Measured with nothing on the graph.
"""
import time

import rclpy
from rclpy.node import Node


def main():
    rclpy.init()
    node = Node("probe_spin_once")
    for timeout in (0.001, 0.01, 0.1, 1.0):
        start = time.monotonic()
        for _ in range(20):
            rclpy.spin_once(node, timeout_sec=timeout)
        elapsed = (time.monotonic() - start) / 20
        print("PROBE spin_once(timeout_sec=%-5s) -> %.4f s per call (x%g off)"
              % (timeout, elapsed, timeout / elapsed if elapsed else 0), flush=True)


main()
