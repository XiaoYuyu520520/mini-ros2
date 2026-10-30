# -*- coding: utf-8 -*-
"""Sixth probe: follow the ring counters from both ends."""
import time

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_msgs.action import NavigateToPose
from ros2py import channel

LIB = channel._library()


def counts(endpoint):
    if endpoint is None:
        return None
    return (LIB.shm_channel_published_count(endpoint._handle),
            LIB.shm_channel_read_count(endpoint._handle))


def run(node, client, label, seconds=10.0):
    goal = NavigateToPose.Goal()
    goal.pose.header.frame_id = 'map'
    goal.pose.pose.position.x = 1.178
    goal.pose.pose.position.y = -1.089
    goal.pose.pose.orientation.w = 1.0
    print("DBG %s ready=%s" % (label, client.server_is_ready()), flush=True)
    print("DBG %s goal ring  before=%s" % (label, counts(client._goal)), flush=True)
    print("DBG %s ack  ring  before=%s" % (label, counts(client._goal_ack)), flush=True)
    fut = client.send_goal_async(goal)
    print("DBG %s goal ring  after publish=%s" % (label, counts(client._goal)), flush=True)
    start = time.monotonic()
    while not fut.done() and time.monotonic() - start < seconds:
        client.poll()
        rclpy.spin_once(node, timeout_sec=0.001)
    print("DBG %s done=%s accepted=%r elapsed=%.1f" % (
        label, fut.done(), client._goals[next(iter(client._goals))].handle.accepted,
        time.monotonic() - start), flush=True)
    print("DBG %s ack  ring  after=%s" % (label, counts(client._goal_ack)), flush=True)


def main():
    rclpy.init()
    from rclpy.action import ActionClient
    from rclpy.node import Node

    plain = Node("probe6_plain")
    simple = ActionClient(plain, NavigateToPose, 'navigate_to_pose')
    run(plain, simple, "plain-node", seconds=6.0)

    from nav2_simple_commander.robot_navigator import BasicNavigator
    nav = BasicNavigator()
    print("DBG navigator clients=%d" % len(nav._action_clients), flush=True)
    run(nav, nav.nav_to_pose_client, "basic-navigator", seconds=10.0)


main()
