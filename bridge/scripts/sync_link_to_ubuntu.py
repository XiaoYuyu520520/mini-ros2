#!/usr/bin/env python3
"""Push the Ubuntu side of the connector to the robot and prepare it.

Covers what the relay needs, which is more than `sync_to_ubuntu.py` sends:

* `bridge/ubuntu/ros2_bridge_sim` - the simulator and its gRPC agent
* `bridge/ubuntu/ros2_link`       - the relay that merges two agents
* `bridge/scripts/connector.sh`   - the connector's entry point
* `start/connector.sh`            - the same thing at the repo root

It then creates the virtualenv the simulator and the relay share and
installs their dependencies.

    python bridge/scripts/sync_link_to_ubuntu.py
    python bridge/scripts/sync_link_to_ubuntu.py --host 192.168.0.153 --user wang
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")

#: (local path relative to the repo root, remote path relative to the root)
PAYLOAD = (
    ("bridge/ubuntu/ros2_bridge_sim", "bridge/ubuntu/ros2_bridge_sim"),
    ("bridge/ubuntu/ros2_link", "bridge/ubuntu/ros2_link"),
    ("start/connector.sh", "start/connector.sh"),
    ("start/加载模拟ROS环境变量.sh", "start/加载模拟ROS环境变量.sh"),
    # The C++ SDK tree is needed for `ros2_grpc_sdk_agent --backend rclcpp`,
    # which is the *real* ROS2 side of the connector. It is a big tree, so it
    # is only synced when it is not there yet; `--force` re-sends it.
    ("gRPC/ros2_gRPC_sdk", "gRPC/ros2_gRPC_sdk"),
    # The SDK's build script imports the workspace's core-detection and
    # toolchain helpers from <repo>/tools, so that directory has to come
    # along or `python3 scripts/build.py` cannot even start.
    ("tools", "tools"),
)


def ssh_base(key: str, port: str) -> list[str]:
    return ["ssh", "-i", key, "-p", port,
            "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new"]


def run(cmd: list[str], **kwargs) -> int:
    print("+ " + " ".join(str(part) for part in cmd), flush=True)
    return subprocess.run([str(part) for part in cmd], **kwargs).returncode


def run_capture(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    """Run a command and hand back the whole result, output included."""
    print("+ " + " ".join(str(part) for part in cmd), flush=True)
    return subprocess.run([str(part) for part in cmd], **kwargs)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--user", default="wang")
    parser.add_argument("--port", default="22")
    parser.add_argument("--key", default=str(DEFAULT_KEY))
    parser.add_argument("--remote-root", default="~/code/CPU")
    parser.add_argument("--no-venv", action="store_true")
    parser.add_argument("--force", action="store_true",
                        help="re-send trees that look like they are already there")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    key = str(args.key)
    if not Path(key).is_file():
        raise SystemExit(f"missing ssh key {key}")
    target = f"{args.user}@{args.host}"
    base = ssh_base(key, args.port)
    root = args.remote_root

    for local, remote in PAYLOAD:
        src = REPO_ROOT / local
        if not src.exists():
            raise SystemExit(f"missing local payload {src}")

    if args.dry_run:
        for local, remote in PAYLOAD:
            print(f"would copy {local} -> {target}:{root}/{remote}")
        return 0

    # The remote tree has to exist before anything is unpacked into it.
    code = run(base + [target, f"mkdir -p {root}/bridge/ubuntu {root}/bridge/scripts {root}/start"])
    if code != 0:
        return code

    # tar over ssh keeps permissions and is one round trip per payload. A
    # directory's archive holds its *contents*, so it is unpacked into the
    # directory itself - unpacking one level up scatters the modules next to
    # the package and the import then picks up the stale copy.
    #
    # Build outputs are excluded: the robot compiles its own, and the
    # Windows build tree in the archive is both huge and useless there.
    excludes = ["--exclude=build", "--exclude=__pycache__", "--exclude=.git",
                "--exclude=*.pyc", "--exclude=build-*"]
    for local, remote in PAYLOAD:
        src = REPO_ROOT / local
        remote_path = f"{root}/{remote}"

        # A tree that is already there is left alone unless --force: the SDK
        # tree is big and rebuilding it is the robot's job anyway.
        if not args.force and src.is_dir():
            probe = run_capture(base + [target, f"test -d {remote_path} && echo PRESENT || echo ABSENT"],
                                capture_output=True, text=True)
            if "PRESENT" in (probe.stdout or ""):
                print(f"= {local} already on {target} - skipped (use --force to re-send)")
                continue

        if src.is_dir():
            extract = f"mkdir -p {remote_path} && tar -xzf - -C {remote_path}"
            tar_args = ["tar", "-czf", "-"] + excludes + ["-C", str(src), "."]
        else:
            parent = remote.rsplit("/", 1)[0]
            extract = f"mkdir -p {root}/{parent} && tar -xzf - -C {root}/{parent}"
            tar_args = ["tar", "-czf", "-"] + excludes + ["-C", str(src.parent), src.name]
        print(f"+ {local} -> {target}:{remote_path}", flush=True)
        producer = subprocess.Popen(tar_args, stdout=subprocess.PIPE)
        extractor = subprocess.Popen(base + [target, extract], stdin=producer.stdout)
        producer.stdout.close()
        producer.wait()
        code = extractor.wait()
        if code != 0:
            return code

    if not args.no_venv:
        # The simulator and the relay share one virtualenv: both only need
        # grpcio / protobuf / numpy, and the relay imports the simulator's
        # proto stubs.
        setup = (
            "set -e; cd {root}; "
            "[ -d .venv ] || python3 -m venv .venv; "
            ".venv/bin/python -m pip install -q --upgrade pip; "
            ".venv/bin/python -m pip install -q -r bridge/ubuntu/ros2_bridge_sim/requirements.txt"
        ).format(root=root)
        code = run(base + [target, setup])
        if code != 0:
            return code

    print(f"synced to {target}:{root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
