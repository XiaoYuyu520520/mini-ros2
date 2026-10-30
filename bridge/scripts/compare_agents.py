#!/usr/bin/env python3
"""Compare the two upstream agents with the merged graph, side by side.

The merged graph is only as good as what it was given: if the merged view
says "pub=0" for a topic, the question is whether the *upstream* also said
0 (the agent's counters are simply not filled in) or whether the relay lost
the publisher information on the way.

    python bridge/scripts/compare_agents.py --host 192.168.0.153
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ubuntu"))

import grpc  # noqa: E402

from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402


def snapshot(stub, label):
    resp = stub.ListTopics(pb.ListTopicsRequest())
    return {t.name: (t.type, t.publisher_count, t.subscriber_count) for t in resp.topics}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--sim-port", type=int, default=50051)
    parser.add_argument("--real-port", type=int, default=50111)
    parser.add_argument("--link-port", type=int, default=50060)
    args = parser.parse_args()

    views = {}
    for label, port in (("sim", args.sim_port), ("real", args.real_port), ("merged", args.link_port)):
        target = "%s:%d" % (args.host, port)
        try:
            with grpc.insecure_channel(target) as channel:
                grpc.channel_ready_future(channel).result(timeout=8)
                views[label] = snapshot(pb_grpc.Ros2BridgeStub(channel), label)
        except Exception as exc:
            print("cannot reach %s on %s: %s" % (label, target, exc))
            views[label] = {}

    names = sorted(set().union(*[set(v) for v in views.values()]) if views else [])
    print("%-32s | %-26s | %-26s | %s" % ("topic", "sim", "real", "merged"))
    print("-" * 110)
    for name in names:
        def cell(view):
            if name not in view:
                return "-"
            _type, pub, sub = view[name]
            return "%s pub=%d sub=%d" % (_type.split("/")[-1], pub, sub)
        print("%-32s | %-26s | %-26s | %s" % (name, cell(views["sim"]), cell(views["real"]), cell(views["merged"])))

    print()
    for label, view in views.items():
        print("%-6s %d topic(s)" % (label, len(view)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
