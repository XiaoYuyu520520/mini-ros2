#!/usr/bin/env python3
"""A genuine ROS2 service server with a type the bridge has a codec for.

`demo_nodes_py/add_two_ints_server` is the obvious service to test against, but
its type is `example_interfaces/srv/AddTwoInts`, which the bridge has no codec
for - so a call would fail at the *table*, not at the transport, and the test
would prove nothing about the transport. `std_srvs/srv/Trigger` is in the table,
so a failure here is a failure of the rclcpp backend's `CallService`.

    python bridge/scripts/ubuntu_ros2_peer.sh start trigger

Answers every request with a counter, so a caller can tell two calls apart.
"""
import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger


class TriggerServer(Node):
    def __init__(self) -> None:
        super().__init__("probe_trigger_server")
        self.count = 0
        # Not called `handle`: `rclpy.node.Node.handle` is a property, and a
        # subclass attribute of that name shadows it for the *base* class too -
        # `Node.__init__` then dies with "'method' object does not support the
        # context manager protocol" before the node ever exists.
        self.create_service(Trigger, "/probe_trigger", self.on_trigger)
        self.get_logger().info("serving /probe_trigger (std_srvs/srv/Trigger)")

    def on_trigger(self, request, response):
        self.count += 1
        response.success = True
        response.message = "probe #%d" % self.count
        self.get_logger().info("answered %r" % response.message)
        return response


def main() -> None:
    rclpy.init()
    node = TriggerServer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
