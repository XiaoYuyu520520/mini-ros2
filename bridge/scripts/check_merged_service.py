#!/usr/bin/env python3
"""Call a service on each side through the merged graph.

Listing a service only shows the relay copied a name; a *call* is what proves
it routes the request to the upstream that serves it and brings the answer
back.

    python bridge/scripts/check_merged_service.py --host 192.168.0.153
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ubuntu"))

import grpc  # noqa: E402

from ros2_bridge_sim import messages as msg  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb  # noqa: E402
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--port", type=int, default=50060)
    args = parser.parse_args()

    target = "%s:%d" % (args.host, args.port)
    rc = 0
    with grpc.insecure_channel(target) as channel:
        grpc.channel_ready_future(channel).result(timeout=10)
        stub = pb_grpc.Ros2BridgeStub(channel)

        services = {s.name: s.type for s in stub.ListServices(pb.ListServicesRequest()).services}

        # 1. The simulator's own Trigger service.
        if "/sim/reset" in services:
            resp = stub.CallService(pb.CallServiceRequest(
                name="/sim/reset", type="std_srvs/srv/Trigger",
                request=msg.encode({}, "std_srvs/srv/Trigger_Request"), encoding="cdr"))
            print("/sim/reset (sim)          success=%s error=%r" % (resp.success, resp.error))
            if resp.success:
                decoded = msg.decode(resp.response, "std_srvs/srv/Trigger_Response")
                print("   -> %r" % decoded)
            else:
                rc = 1
        else:
            print("/sim/reset not advertised", file=sys.stderr)
            rc = 1

        # 2. The real ROS2 graph's AddTwoInts. `example_interfaces` is not a
        #    type the simulator implements, so it is encoded the way the real
        #    agent expects (a CDR int64 + int64, 4 byte encapsulation header).
        if "/add_two_ints" in services:
            # example_interfaces/srv/AddTwoInts_Request { int64 a; int64 b }
            body = bytes([0x00, 0x01, 0x00, 0x00]) + (2).to_bytes(8, "little", signed=True) \
                   + (40).to_bytes(8, "little", signed=True)
            resp = stub.CallService(pb.CallServiceRequest(
                name="/add_two_ints", type="example_interfaces/srv/AddTwoInts",
                request=body, encoding="cdr"))
            print("/add_two_ints (real)      success=%s error=%r" % (resp.success, resp.error))
            if resp.success:
                # example_interfaces/srv/AddTwoInts_Response { int64 sum }
                total = int.from_bytes(resp.response[4:12], "little", signed=True)
                print("   -> 2 + 40 = %d" % total)
                if total != 42:
                    rc = 1
            else:
                rc = 1
        else:
            print("/add_two_ints not advertised", file=sys.stderr)
            rc = 1

    print("RESULT:", "PASS" if rc == 0 else "FAIL")
    return rc


if __name__ == "__main__":
    sys.exit(main())
