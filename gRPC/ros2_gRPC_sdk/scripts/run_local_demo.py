#!/usr/bin/env python3
"""Start the mock agent and drive it with the example clients over gRPC.

A single-host rehearsal of the Windows <-> Linux setup. The launcher detects
the current platform, finds the matching build directory (via
``build/platforms.yaml``) and starts the right executables (``.exe`` on
Windows, no suffix elsewhere). Everything is launched with subprocess - no
PowerShell or cmd batch files.

    python scripts/run_local_demo.py
    python scripts/run_local_demo.py --platform windows-x86_64
    python scripts/run_local_demo.py --build-dir build/linux-x86_64
"""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from platform_utils import (  # noqa: E402
    BUILD_ROOT,
    ROOT,
    exe_suffix,
    host_key,
    resolve_build_dir,
)
import config as sdk_config  # noqa: E402


def log(message: str) -> None:
    print(f"[demo] {message}", flush=True)


def tool(build_dir: Path, platform_key: str | None, name: str) -> Path:
    suffix = exe_suffix(platform_key)
    candidates = [
        build_dir / f"ros2_grpc_sdk_{name}{suffix}",
        build_dir / f"ros2_grpc_sdk_{name}.exe",
        build_dir / f"ros2_grpc_sdk_{name}",
    ]
    for path in candidates:
        if path.is_file():
            return path
    raise SystemExit(
        f"missing ros2_grpc_sdk_{name} under {build_dir}; "
        "build first with scripts/build.py"
    )


def wait_for_port(host: str, port: int, timeout_s: float = 10.0) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Local ros2_gRPC_sdk demo")
    parser.add_argument(
        "--platform",
        default=None,
        help="platform key to launch (default: auto-detect the current host)",
    )
    parser.add_argument(
        "--build-root", default=str(BUILD_ROOT.name), help="build root directory"
    )
    parser.add_argument(
        "--build-dir",
        default=None,
        help="explicit build directory (overrides platform detection)",
    )
    parser.add_argument("--address", default=None, help="agent bind address (default: config)")
    parser.add_argument("--connect", default=None, help="client target (default: from address)")
    parser.add_argument("--subscribe-seconds", type=int, default=3)
    args = parser.parse_args()

    config = sdk_config.load_config()
    if args.address is None:
        args.address = sdk_config.server_address(config)
    if args.connect is None:
        host, _, port = args.address.rpartition(":")
        if host in ("", "0.0.0.0", "::"):
            host = "localhost"
        args.connect = f"{host}:{port}"

    build_root = (ROOT / args.build_root).resolve()
    if args.build_dir:
        build_dir = (ROOT / args.build_dir).resolve()
        platform_key = args.platform
    else:
        platform_key = args.platform or host_key()
        build_dir = resolve_build_dir(platform_key, build_root)

    log(f"host={host_key()} platform={platform_key} build_dir={build_dir}")

    agent = tool(build_dir, platform_key, "mock_agent")
    list_topics = tool(build_dir, platform_key, "list_topics")
    publish = tool(build_dir, platform_key, "publish")
    subscribe = tool(build_dir, platform_key, "subscribe")

    host, _, port_text = args.address.rpartition(":")
    if not host or host == "0.0.0.0":
        host = "127.0.0.1"
    port = int(port_text)

    agent_log = build_dir / "agent_demo.log"
    with agent_log.open("w", encoding="utf-8", errors="replace") as out:
        log(f"starting mock agent on {args.address}")
        agent_proc = subprocess.Popen(
            [str(agent), args.address], stdout=out, stderr=subprocess.STDOUT
        )

        try:
            if not wait_for_port(host, port):
                raise SystemExit(f"agent did not open {host}:{port}; see {agent_log}")

            log("== list topics ==")
            subprocess.run([str(list_topics), args.connect], check=True)

            log("== publish ==")
            subprocess.run(
                [
                    str(publish),
                    args.connect,
                    "/demo/echo",
                    "std_msgs/msg/String",
                    "hello from the demo script",
                ],
                check=True,
            )

            log(f"== subscribe to /demo/chatter for {args.subscribe_seconds}s ==")
            subprocess.run(
                [
                    str(subscribe),
                    args.connect,
                    "/demo/chatter",
                    "std_msgs/msg/String",
                    str(args.subscribe_seconds),
                ],
                check=True,
            )
        finally:
            log("stopping agent")
            agent_proc.terminate()
            try:
                agent_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                agent_proc.kill()
                agent_proc.wait(timeout=5)

    return 0


if __name__ == "__main__":
    sys.exit(main())
