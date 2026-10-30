# -*- coding: utf-8 -*-
"""Why does AMCL reject a Python /initialpose as malformed?

`nav2::validateMsg(PoseWithCovarianceStamped)` checks the header (non-empty
frame_id, nanosec < 1e9), the position, the covariance (0 <= c <= 1e9) and the
quaternion *normalised to within 1e-4*. `BasicNavigator.initial_pose` is a
default constructed `PoseStamped`, whose quaternion is (0,0,0,0) - norm 0, so
that check fails. This probe publishes both spellings and counts /amcl_pose.
"""
import time

import rclpy
from geometry_msgs.msg import PoseWithCovarianceStamped
from rclpy.node import Node

received = []


def make(frame, w, covariance):
    msg = PoseWithCovarianceStamped()
    msg.header.frame_id = frame
    msg.pose.pose.orientation.w = w
    if covariance is not None:
        msg.pose.covariance = list(covariance)
    return msg


def main():
    rclpy.init()
    node = Node("probe_initialpose")
    node.create_subscription(PoseWithCovarianceStamped, "/amcl_pose",
                            lambda m: received.append(m), 10)
    pub = node.create_publisher(PoseWithCovarianceStamped, "/initialpose", 10)

    cases = [
        ("frame=map, w=0.0 (what BasicNavigator sends)",
         make("map", 0.0, None)),
        ("frame=map, w=1.0",
         make("map", 1.0, None)),
        ("frame=map, w=1.0, covariance 0.25/0.25/0.06",
         make("map", 1.0, [0.25, 0.0, 0.0, 0.0, 0.0, 0.0,
                           0.0, 0.25, 0.0, 0.0, 0.0, 0.0,
                           0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                           0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                           0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                           0.0, 0.0, 0.0, 0.0, 0.0, 0.06])),
        ("frame='' (empty), w=1.0",
         make("", 1.0, None)),
    ]
    for label, msg in cases:
        msg.header.stamp = node.get_clock().now().to_msg()
        before = len(received)
        for _ in range(10):
            pub.publish(msg)
            rclpy.spin_once(node, timeout_sec=0.02)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.02)
        print("PROBE %-46s /amcl_pose +%d" % (label, len(received) - before), flush=True)


main()
