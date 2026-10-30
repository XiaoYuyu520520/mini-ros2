# -*- coding: utf-8 -*-
"""Third probe: is the Python process even on the same channels?"""
import os

import rclpy
from ros2py import channel, graph

print("DBG NAV2_SHM_NAMESPACE=%r" % (os.environ.get("NAV2_SHM_NAMESPACE"),), flush=True)
print("DBG NAV2_SHM_GRAPH_DIR=%r" % (os.environ.get("NAV2_SHM_GRAPH_DIR"),), flush=True)
print("DBG ROS2_BRIDGE_AGENT=%r" % (os.environ.get("ROS2_BRIDGE_AGENT"),), flush=True)
for topic in ("/navigate_to_pose/_goal", "/navigate_to_pose/_goal_ack",
              "/navigate_to_pose/_result", "/navigate_to_pose/_action/status", "/odom"):
    opened = channel.Channel.open(topic)
    print("DBG open %-38s -> %r (slot %d)" % (
        topic, opened is not None, channel.slot_size_for_topic(topic)), flush=True)
    if opened is not None:
        opened.close()
registry = graph.read()
print("DBG action_exists=%r" % (registry.action_exists("/navigate_to_pose"),), flush=True)
print("DBG publishers of ack=%r" % (registry.publisher_count("/navigate_to_pose/_goal_ack"),), flush=True)
print("DBG publishers of goal=%r" % (registry.publisher_count("/navigate_to_pose/_goal"),), flush=True)
