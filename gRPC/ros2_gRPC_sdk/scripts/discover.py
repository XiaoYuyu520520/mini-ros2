#!/usr/bin/env python3
"""Run the LAN discovery example for the current platform.

Detects the platform, finds the matching build directory and launches
``ros2_grpc_sdk_discover`` with the requested options. Ports/timeouts default
to the values in the config folder.

    python scripts/discover.py
    python scripts/discover.py --ports 50051,50052 --timeout 3000
    python scripts/discover.py --subnet 192.168.1.0/24
    python scripts/discover.py --self --subnet 127.0.0.1/32
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import config as sdk_config  # noqa: E402
from platform_utils import (  # noqa: E402
    BUILD_ROOT,
    ROOT,
    exe_suffix,
    host_key,
    resolve_build_dir,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run LAN discovery")
    parser.add_argument("--platform", default=None)
    parser.add_argument("--build-root", default=str(BUILD_ROOT.name))
    parser.add_argument("--build-dir", default=None)
    parser.add_argument("--ports", default=None, help="comma separated TCP ports")
    parser.add_argument("--timeout", type=int, default=None, help="overall budget in ms")
    parser.add_argument("--subnet", default=None, help="explicit CIDR to scan")
    parser.add_argument("--self", dest="include_self", action="store_true",
                        help="include this machine / loopback")
    parser.add_argument("--config", dest="config_path", default=None)
    args = parser.parse_args()

    build_root = (ROOT / args.build_root).resolve()
    if args.build_dir:
        build_dir = (ROOT / args.build_dir).resolve()
        platform_key = args.platform
    else:
        platform_key = args.platform or host_key()
        build_dir = resolve_build_dir(platform_key, build_root)

    exe = build_dir / f"ros2_grpc_sdk_discover{exe_suffix(platform_key)}"
    if not exe.is_file():
        raise SystemExit(f"missing {exe}; build first with scripts/build.py")

    if args.ports is None:
        args.ports = ",".join(str(p) for p in sdk_config.discovery_ports())
    if args.timeout is None:
        args.timeout = int(sdk_config.discovery_options()["overall_timeout_ms"])

    cmd = [str(exe), args.ports, str(args.timeout)]
    if args.subnet:
        cmd.append(args.subnet)
    if args.include_self:
        cmd.append("--self")
    if args.config_path:
        cmd += ["--config", args.config_path]

    print(f"[discover] host={host_key()} build_dir={build_dir}")
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
