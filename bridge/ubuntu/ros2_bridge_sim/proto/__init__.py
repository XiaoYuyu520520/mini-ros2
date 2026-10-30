"""Generated gRPC stubs for ``ros2bridge.Ros2Bridge``.

Regenerate from the repository root with::

    python -m grpc_tools.protoc -I gRPC/ros2_gRPC_sdk/proto \
        --python_out=bridge/ubuntu/ros2_bridge_sim/proto \
        --grpc_python_out=bridge/ubuntu/ros2_bridge_sim/proto \
        gRPC/ros2_gRPC_sdk/proto/ros2_bridge.proto

The only post-processing is that ``ros2_bridge_pb2_grpc.py`` imports the
messages relatively, so that ``from ros2_bridge_sim.proto import
ros2_bridge_pb2`` works from any working directory.
"""

from . import ros2_bridge_pb2, ros2_bridge_pb2_grpc

__all__ = ["ros2_bridge_pb2", "ros2_bridge_pb2_grpc"]
