#!/usr/bin/env python3
"""Start the real-ROS2 peer and the agent on the robot, then report.

The real ROS2 side of the connector is
`ros2_grpc_sdk_agent --backend rclcpp`: it sits inside a genuine ROS2 graph
and gives that graph a gRPC face. Next to it a few demo nodes are started so
the graph has something in it.

    python bridge/scripts/check_ubuntu_real_ros.py
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")


def ssh(target, key, port, script, timeout=120):
    return subprocess.run(
        ["ssh", "-i", key, "-p", port, "-o", "BatchMode=yes",
         "-o", "StrictHostKeyChecking=accept-new", target, "bash -s"],
        input=script.encode("utf-8"), capture_output=True, timeout=timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--user", default="wang")
    parser.add_argument("--port", default="22")
    parser.add_argument("--key", default=str(DEFAULT_KEY))
    parser.add_argument("--remote-root", default="~/code/CPU")
    parser.add_argument("--agent-port", default="50111")
    args = parser.parse_args()

    key = str(args.key)
    target = f"{args.user}@{args.host}"
    root = args.remote_root

    script = "\n".join([
        f"cd {root}",
        ". /opt/ros/jazzy/setup.bash",
        "echo '=== stop any previous peer ==='",
        "pkill -f ros2_grpc_sdk_agent 2>/dev/null || true",
        "pkill -f demo_nodes_cpp 2>/dev/null || true",
        "pkill -f fibonacci_action_server 2>/dev/null || true",
        "sleep 1",
        "mkdir -p ~/ros2_peer",
        "echo '=== start the demo nodes (real ROS2 graph) ==='",
        "setsid nohup ros2 run demo_nodes_cpp talker > ~/ros2_peer/talker.log 2>&1 < /dev/null &",
        "setsid nohup ros2 run demo_nodes_py add_two_ints_server > ~/ros2_peer/service.log 2>&1 < /dev/null &",
        "setsid nohup ros2 run action_tutorials_cpp fibonacci_action_server > ~/ros2_peer/action.log 2>&1 < /dev/null &",
        "sleep 4",
        "echo '=== start the rclcpp agent ==='",
        f"AGENT={root}/gRPC/ros2_gRPC_sdk/build/linux-x86_64/ros2_grpc_sdk_agent",
        "if [ ! -x \"$AGENT\" ]; then echo \"AGENT MISSING: $AGENT\"; exit 1; fi",
        f"setsid nohup \"$AGENT\" 0.0.0.0:{args.agent_port} --backend rclcpp "
        "> ~/ros2_peer/agent.log 2>&1 < /dev/null &",
        "sleep 4",
        "echo '--- agent.log ---'",
        "tail -n 15 ~/ros2_peer/agent.log",
        "echo '--- ros2 topic list (the real graph) ---'",
        "timeout 15 ros2 topic list 2>&1 | head -20",
        "echo '--- listening ---'",
        f"ss -lnt 2>/dev/null | grep ':{args.agent_port} ' || echo 'NOT LISTENING'",
    ]) + "\n"

    r = ssh(target, key, args.port, script, timeout=180)
    print(r.stdout.decode("utf-8", "replace"))
    e = r.stderr.decode("utf-8", "replace")
    if e.strip():
        print("STDERR:", e[:800], file=sys.stderr)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
