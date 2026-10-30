# -*- coding: utf-8 -*-
"""Diagnostic: why does BasicNavigator report "was rejected"?"""
import time

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_simple_commander.robot_navigator import BasicNavigator


def main():
    rclpy.init()
    nav = BasicNavigator()
    client = nav.nav_to_pose_client
    print("DBG node", nav.get_name(), flush=True)
    print("DBG client", client, "ready", client.server_is_ready(), flush=True)
    print("DBG channels goal=%r ack=%r" % (client._goal is not None,
                                           client._goal_ack is not None), flush=True)
    goal = PoseStamped()
    goal.header.frame_id = 'map'
    goal.header.stamp = nav.get_clock().now().to_msg()
    goal.pose.position.x = 1.178
    goal.pose.position.y = -1.089
    goal.pose.orientation.w = 1.0
    print("DBG stamp", goal.header.stamp.sec, goal.header.stamp.nanosec, flush=True)
    fut = client.send_goal_async(goal)
    print("DBG sent future=%r pending=%s" % (fut, list(client._goals.keys())), flush=True)
    start = time.monotonic()
    while not fut.done() and time.monotonic() - start < 15.0:
        rclpy.spin_once(nav, timeout_sec=0.001)
    print("DBG done=%s elapsed=%.2f" % (fut.done(), time.monotonic() - start), flush=True)
    handle = fut.result()
    print("DBG handle=%r accepted=%r status=%r" % (
        handle, getattr(handle, 'accepted', None), getattr(handle, 'status', None)), flush=True)
    print("DBG exceptions=%r" % (fut.exception(),), flush=True)


main()
