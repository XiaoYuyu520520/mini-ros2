# -*- coding: utf-8 -*-
"""Print the planned path waypoints from an explicit start to a goal."""
import math
import sys
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from geometry_msgs.msg import PoseStamped
from nav2_msgs.action import ComputePathToPose


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
        rclpy.spin_once(node, timeout_sec=0.001)
    return fut.done()


sx, sy, syaw, gx, gy, gyaw = [float(a) for a in sys.argv[1:7]]
rclpy.init()
node = Node("probe_path")
client = ActionClient(node, ComputePathToPose, "compute_path_to_pose")
client.wait_for_server(timeout_sec=5.0)
goal = ComputePathToPose.Goal()
goal.use_start = True
goal.start = pose(sx, sy, syaw)
goal.goal = pose(gx, gy, gyaw)
t0 = time.monotonic()
fut = client.send_goal_async(goal)
spin_until(node, client, fut, 20.0)
handle = fut.result()
rfut = handle.get_result_async()
spin_until(node, client, rfut, 60.0)
res = rfut.result().result
print("PROBE t=%.3f poses=%d" % (time.monotonic() - t0, len(res.path.poses)), flush=True)
for i, p in enumerate(res.path.poses):
    print("PROBE %2d  %6.2f %6.2f" % (i, p.pose.position.x, p.pose.position.y), flush=True)
