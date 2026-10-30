#!/usr/bin/env python3
"""Configure (if needed) + build + run nav2/shm_transport, on the six big cores.

One entry point for both platforms: the toolchain comes from tools/_toolchain.py
and the job count from tools/_cores.py, the same two modules the shim, the
bridge and the gRPC SDK build scripts use.

    python nav2/shm_transport/build.py               # configure + build + ctest
    python nav2/shm_transport/build.py --no-test     # just build
    python nav2/shm_transport/build.py --build-dir nav2/shm_transport/build-dbg
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from _cores import big_cores, pin  # noqa: E402
from _toolchain import build_env, describe, find_tools  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", default=str(HERE / "build"))
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--jobs", type=int, default=6, help="default 6 (the big cores)")
    parser.add_argument("--msys2", default=None, help="MSYS2 root (Windows only)")
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument("--reconfigure", action="store_true")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (Path.cwd() / build_dir).resolve()

    tools = find_tools(args.msys2)
    env = build_env(tools)
    print("[shm] %s" % describe(tools))
    print("[shm] %s" % pin())

    def run(cmd: list[str]) -> int:
        print("[shm] %s" % " ".join(cmd))
        return subprocess.call(cmd, env=env, cwd=str(HERE))

    if args.reconfigure or not (build_dir / "CMakeCache.txt").is_file():
        rc = run([str(tools.cmake), "-S", str(HERE), "-B", str(build_dir), "-G", "Ninja",
                  "-DCMAKE_BUILD_TYPE=" + args.build_type])
        if rc != 0:
            return rc

    rc = run([str(tools.cmake), "--build", str(build_dir), "-j", str(args.jobs)])
    if rc != 0 or args.no_test:
        return rc
    return run([str(tools.ctest), "--test-dir", str(build_dir), "--output-on-failure"])


if __name__ == "__main__":
    sys.exit(main())