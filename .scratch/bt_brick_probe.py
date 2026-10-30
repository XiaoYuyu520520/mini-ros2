# -*- coding: utf-8 -*-
"""Does one unsatisfiable goal still brick bt_navigator?

Sends a goal that cannot be planned (inside the wall), waits for its result, then
sends a perfectly good one and reports whether it is accepted and what it returns.
"""
import math
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from geometry_msgs.msg import PoseStamped
from nav2_msgs.action import NavigateToPose

BAD = (0.50, 2.00, 0.0)
GOOD = (2.00, -1.00, 0.0)


def pose(x, y, yaw):
    p = PoseStamped()
    p.header.frame_id = "map"
    p.pose.position.x = float(x)
    p.pose.position.y = float(y)
    p.pose.orientation.z = math.sin(yaw / 2.0)
    p.pose.orientation.w = math.cos(yaw / 2.0)
    return p


def spin_until(node, client, fut, seconds):
    end = time.monotonic() + seconds
    while not fut.done() and time.monotonic() < end:
        client.poll()
        rclpy.spin_once(node, timeout_sec=0.002)
    return fut.done()


rclpy.init()
node = Node("bt_brick_probe")
client = ActionClient(node, NavigateToPose, "navigate_to_pose")
print("PROBE server=%s" % client.wait_for_server(timeout_sec=10.0), flush=True)

for label, (x, y, yaw) in (("bad", BAD), ("good", GOOD)):
    goal = NavigateToPose.Goal()
    goal.pose = pose(x, y, yaw)
    t0 = time.monotonic()
    fut = client.send_goal_async(goal)
    spin_until(node, client, fut, 30.0)
    handle = fut.result()
    accepted = bool(handle is not None and handle.accepted)
    print("PROBE %s goal (%s) accepted=%s t=%.2f" % (label, "%g,%g,%g" % (x, y, yaw), accepted,
                                                     time.monotonic() - t0), flush=True)
    if not accepted:
        continue
    rfut = handle.get_result_async()
    done = spin_until(node, client, rfut, 240.0)
    dt = time.monotonic() - t0
    if not done:
        print("PROBE %s goal NO RESULT after %.1f s" % (label, dt), flush=True)
        continue
    w = rfut.result()
    print("PROBE %s goal status=%s code=%s err=%s t=%.1f" % (
        label, w.status, w.code, getattr(w.result, "error_code", "?"), dt), flush=True)

rclpy.shutdown()
