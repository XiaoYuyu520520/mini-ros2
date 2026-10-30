# -*- coding: utf-8 -*-
"""Second probe: who is eating the ack, and is anyone polling?"""
import time

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_simple_commander.robot_navigator import BasicNavigator


def main():
    rclpy.init()
    nav = BasicNavigator()
    client = nav.nav_to_pose_client
    print("DBG action_name=%r type=%r" % (client.action_name, client.type_name), flush=True)
    print("DBG clients on node=%d" % len(nav._action_clients), flush=True)
    for other in nav._action_clients:
        print("    %s ack=%r" % (other.action_name, other._goal_ack is not None), flush=True)
    goal = PoseStamped()
    goal.header.frame_id = 'map'
    goal.pose.position.x = 1.178
    goal.pose.position.y = -1.089
    goal.pose.orientation.w = 1.0
    fut = client.send_goal_async(goal)
    pending = client._goals[1]
    print("DBG sent", flush=True)
    start = time.monotonic()
    while not fut.done() and time.monotonic() - start < 12.0:
        handled = client.poll()
        if handled:
            print("DBG client.poll handled=%d at %.2f" % (handled, time.monotonic() - start), flush=True)
        rclpy.spin_once(nav, timeout_sec=0.001)
    print("DBG done=%s elapsed=%.2f accepted=%r" % (
        fut.done(), time.monotonic() - start, pending.handle.accepted), flush=True)
    print("DBG ack endpoint=%r" % (client._goal_ack,), flush=True)
    raw = client._goal_ack.poll()
    print("DBG leftover ack raw=%r" % (raw,), flush=True)


main()
