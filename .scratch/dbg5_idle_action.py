# -*- coding: utf-8 -*-
"""Fifth probe: an idle action server, and the exact bytes Python sends."""
import time

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_msgs.action import ComputePathToPose, NavigateToPose


def send(nav, client, goal, label, seconds=8.0):
    print("DBG %s serialize=%s" % (label, goal.serialize().hex()), flush=True)
    fut = client.send_goal_async(goal)
    pending = client._goals[next(iter(client._goals))]
    start = time.monotonic()
    while not fut.done() and time.monotonic() - start < seconds:
        client.poll()
        rclpy.spin_once(nav, timeout_sec=0.001)
    print("DBG %s done=%s accepted=%r exception=%r" % (
        label, fut.done(), pending.handle.accepted, fut.exception()), flush=True)


def main():
    rclpy.init()
    from rclpy.node import Node
    node = Node("probe5")
    pose = PoseStamped()
    pose.header.frame_id = 'map'
    pose.pose.position.x = 1.178
    pose.pose.position.y = -1.089
    pose.pose.orientation.w = 1.0

    planner = ComputePathToPose  # noqa: F841  (imported for the type name)
    from rclpy.action import ActionClient
    plan_client = ActionClient(node, ComputePathToPose, 'compute_path_to_pose')
    goal = ComputePathToPose.Goal()
    goal.goal = pose
    send(node, plan_client, goal, "compute_path_to_pose")

    nav_client = ActionClient(node, NavigateToPose, 'navigate_to_pose')
    goal2 = NavigateToPose.Goal()
    goal2.pose = pose
    send(node, nav_client, goal2, "navigate_to_pose")


main()
