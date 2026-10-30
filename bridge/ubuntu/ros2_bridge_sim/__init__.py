"""Pure Python ROS2 simulator plus a gRPC bridge agent.

Runs on Ubuntu with only ``grpcio``, ``protobuf`` and ``numpy`` -- no rclpy and
no ROS2 installation.  See ``README.md`` for the design and the CDR contract.

Modules
-------
cdr        CDR1 little endian reader/writer (the wire format)
messages   encoders/decoders for the ROS2 message types in use
graph      tiny in-process pub/sub graph
world      2D world: obstacles, OccupancyGrid, ray-cast lidar
sim        differential drive car and a simple goal driver
agent      gRPC ``Ros2Bridge`` service over the graph
run_sim    CLI entry point (sim + agent + interactive console)
"""

__version__ = "0.1"

AGENT_VERSION = "ros2_bridge_sim/0.1"
ROS_DISTRO = "sim"
