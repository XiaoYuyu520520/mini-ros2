# -*- coding: utf-8 -*-
"""Does a Python subscriber see /amcl_pose? Both QoS spellings."""
import time

import rclpy
from geometry_msgs.msg import PoseWithCovarianceStamped
from rclpy.node import Node
from rclpy.qos import (QoSHistoryPolicy, QoSProfile, QoSReliabilityPolicy,
                       QoSDurabilityPolicy)

got_default = []
got_nav2 = []


def main():
    rclpy.init()
    node = Node("probe_amcl_pose")
    node.create_subscription(PoseWithCovarianceStamped, "/amcl_pose",
                            lambda m: got_default.append(m), 10)
    nav2_qos = QoSProfile(durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
                          reliability=QoSReliabilityPolicy.RELIABLE,
                          history=QoSHistoryPolicy.KEEP_LAST, depth=1)
    node.create_subscription(PoseWithCovarianceStamped, "/amcl_pose",
                            lambda m: got_nav2.append(m), nav2_qos)

    deadline = time.monotonic() + 6.0
    spins = 0
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
        spins += 1
    print("PROBE spin_once calls        %d" % spins, flush=True)
    print("PROBE depth-10 subscriber    %d message(s)" % len(got_default), flush=True)
    print("PROBE nav2-QoS subscriber    %d message(s)" % len(got_nav2), flush=True)
    if got_default:
        p = got_default[-1].pose.pose.position
        print("PROBE last pose              %.3f %.3f  frame=%s"
              % (p.x, p.y, got_default[-1].header.frame_id), flush=True)


main()
