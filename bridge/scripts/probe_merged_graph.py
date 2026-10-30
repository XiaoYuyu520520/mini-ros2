#!/usr/bin/env python3
"""Ask the merged graph on the robot what it contains.

This is the Windows side of the end-to-end check: the relay listens on the
robot's LAN address, so a plain gRPC client here reaches the merged graph
exactly the way the bridge daemon will.

    python bridge/scripts/probe_merged_graph.py --host 192.168.0.153
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ubuntu"))

import grpc  # noqa: E402

from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--port", type=int, default=50060)
    parser.add_argument("--watch", default="", help="also stream this topic for a few seconds")
    parser.add_argument("--seconds", type=float, default=5.0)
    args = parser.parse_args()

    target = "%s:%d" % (args.host, args.port)
    with grpc.insecure_channel(target) as channel:
        grpc.channel_ready_future(channel).result(timeout=10)
        stub = pb_grpc.Ros2BridgeStub(channel)

        ping = stub.Ping(pb.PingRequest(payload="windows"))
        print("Ping  -> agent_version=%s ros_distro=%s" % (ping.agent_version, ping.ros_distro))

        topics = stub.ListTopics(pb.ListTopicsRequest()).topics
        print("\nmerged graph: %d topic(s)" % len(topics))
        for t in sorted(topics, key=lambda x: x.name):
            print("  %-32s %-44s pub=%d sub=%d"
                  % (t.name, t.type, t.publisher_count, t.subscriber_count))

        services = stub.ListServices(pb.ListServicesRequest()).services
        print("\nmerged services: %d" % len(services))
        for s in sorted(services, key=lambda x: x.name):
            print("  %-32s %s" % (s.name, s.type))

        actions = stub.ListActions(pb.ListActionsRequest()).actions
        print("\nmerged actions: %d" % len(actions))
        for a in sorted(actions, key=lambda x: x.name):
            print("  %-32s %s" % (a.name, a.type))

        if args.watch:
            print("\nstreaming %s for %.1fs ..." % (args.watch, args.seconds))
            got = 0
            try:
                for message in stub.Subscribe(pb.SubscribeRequest(topic=args.watch)):
                    got += 1
                    print("  #%d type=%s bytes=%d stamp_ns=%d"
                          % (got, message.type, len(message.data), message.stamp_ns))
                    if got >= 5:
                        break
            except grpc.RpcError as exc:
                print("  stream ended: %s" % exc.code())
            if got == 0:
                print("  (nothing received)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
