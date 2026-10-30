# -*- coding: utf-8 -*-
"""Probe: drive ComputePathToPose directly with an explicit start and time it.

usage: python shim_run.py .scratch/probe_plan.py [label] x y yaw  (goal pose)
"""
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
    p.pose.position.z = 0.0
    p.pose.orientation.z = math.sin(yaw / 2.0)
    p.pose.orientation.w = math.cos(yaw / 2.0)
    return p


def spin_until(node, client, fut, seconds):
    end = time.monotonic() + seconds
    while not fut.done() and time.monotonic() < end:
        client.poll()
        rclpy.spin_once(node, timeout_sec=0.001)
    return fut.done()


def main():
    start_x = float(sys.argv[1]); start_y = float(sys.argv[2]); start_yaw = float(sys.argv[3])
    goal_x = float(sys.argv[4]); goal_y = float(sys.argv[5]); goal_yaw = float(sys.argv[6])
    rounds = int(sys.argv[7]) if len(sys.argv) > 7 else 3
    rclpy.init()
    node = Node("probe_plan")
    client = ActionClient(node, ComputePathToPose, "compute_path_to_pose")
    ok = client.wait_for_server(timeout_sec=5.0)
    print("PROBE server_ready=%s" % ok, flush=True)
    for i in range(rounds):
        goal = ComputePathToPose.Goal()
        goal.use_start = True
        goal.start = pose(start_x, start_y, start_yaw)
        goal.goal = pose(goal_x, goal_y, goal_yaw)
        t0 = time.monotonic()
        fut = client.send_goal_async(goal)
        accepted = spin_until(node, client, fut, 20.0)
        handle = fut.result() if accepted else None
        if handle is None or not handle.accepted:
            print("PROBE %d accepted=%s t=%.3f" % (
                i, handle is not None and handle.accepted, time.monotonic() - t0), flush=True)
            continue
        rfut = handle.get_result_async()
        done = spin_until(node, client, rfut, 60.0)
        dt = time.monotonic() - t0
        if not done:
            print("PROBE %d TIMEOUT t=%.3f" % (i, dt), flush=True)
            continue
        wrapped = rfut.result()
        result = wrapped.result
        nposes = len(result.path.poses) if result is not None and result.path is not None else -1
        print("PROBE %d status=%s code=%s poses=%d t=%.3f" % (
            i, wrapped.status, wrapped.code, nposes, dt), flush=True)


main()
