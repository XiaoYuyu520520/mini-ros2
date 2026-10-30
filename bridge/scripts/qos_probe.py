#!/usr/bin/env python3
"""Publish a best_effort scan and a transient_local map for the QoS check.

Run this on the Ubuntu peer (after `source /opt/ros/<distro>/setup.bash`):

    python3 bridge/scripts/qos_probe.py

It publishes:

* `/qos_probe/scan`  sensor_msgs/msg/LaserScan   best_effort, volatile, depth 5
* `/qos_probe/map`   nav_msgs/msg/OccupancyGrid  reliable, transient_local, depth 1
* a static `map -> laser` transform, so the scan can be shown in the map frame

The topic names are namespaced so this can run next to a live robot. The
agent's default table (`qosFor` in gRPC/ros2_gRPC_sdk/src/server/rclcpp_backend.cpp)
recognises a sensor type by type and a map by any topic ending in `/map`, so
these names exercise the same two rules as `/scan` and `/map`.

On Windows, with the daemon connected to this peer:

    ros2 topic echo /qos_probe/scan --count 3 --timeout 15
    ros2 topic echo /qos_probe/map --once --timeout 15

Both must print a message. A reliable/volatile agent would print nothing for
the scan (the publisher offers best_effort) and would miss the map if it
subscribed after the first publication.
"""
import math

import rclpy
from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import OccupancyGrid
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import LaserScan
from tf2_ros import StaticTransformBroadcaster


def make_scan(node: Node) -> LaserScan:
    message = LaserScan()
    message.header.stamp = node.get_clock().now().to_msg()
    message.header.frame_id = "laser"
    message.angle_min = -1.0
    message.angle_max = 1.0
    message.angle_increment = 0.1
    message.range_min = 0.1
    message.range_max = 20.0
    message.ranges = [
        2.0 + 0.5 * math.sin(i * 0.3) for i in range(21)
    ]
    return message


def make_map(node: Node) -> OccupancyGrid:
    width = height = 20
    message = OccupancyGrid()
    message.header.stamp = node.get_clock().now().to_msg()
    message.header.frame_id = "map"
    message.info.resolution = 0.5
    message.info.width = width
    message.info.height = height
    message.info.origin.orientation.w = 1.0
    cells = []
    for y in range(height):
        for x in range(width):
            border = x in (0, width - 1) or y in (0, height - 1)
            cells.append(100 if border else 0)
    message.data = cells
    return message


def main() -> None:
    rclpy.init()
    node = Node("qos_probe")

    scan_qos = QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=5,
        reliability=ReliabilityPolicy.BEST_EFFORT,
        durability=DurabilityPolicy.VOLATILE,
    )
    map_qos = QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=1,
        reliability=ReliabilityPolicy.RELIABLE,
        durability=DurabilityPolicy.TRANSIENT_LOCAL,
    )

    scan_publisher = node.create_publisher(LaserScan, "/qos_probe/scan", scan_qos)
    map_publisher = node.create_publisher(OccupancyGrid, "/qos_probe/map", map_qos)

    static = StaticTransformBroadcaster(node)
    transform = TransformStamped()
    transform.header.stamp = node.get_clock().now().to_msg()
    transform.header.frame_id = "map"
    transform.child_frame_id = "laser"
    transform.transform.translation.x = 2.0
    transform.transform.rotation.w = 1.0
    static.sendTransform(transform)

    def publish_map() -> None:
        map_publisher.publish(make_map(node))

    # Once now, then every two seconds: a late transient_local subscriber gets
    # the latched sample; a volatile one would miss every one it was not
    # already there for.
    publish_map()
    node.create_timer(2.0, publish_map)
    node.create_timer(0.1, lambda: scan_publisher.publish(make_scan(node)))

    node.get_logger().info(
        "publishing /qos_probe/scan (best_effort) and /qos_probe/map "
        "(transient_local); Ctrl-C to stop")
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
