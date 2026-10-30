#!/usr/bin/env python3
"""Configure and build the Windows side of the ROS2 bridge.

On Windows the bridge links against gRPC/protobuf from MSYS2 UCRT64, so the
compiler and the toolchain ``bin`` directory have to come from there; elsewhere
the system toolchain is used. Either way everything is driven from this script,
so the build can be reproduced with one command::

    python bridge/scripts/build_bridge.py            # configure + build
    python bridge/scripts/build_bridge.py --clean    # wipe build/ first
    python bridge/scripts/build_bridge.py --test     # ... then run ctest

On Linux the same command builds the same sources against the system gRPC /
protobuf / yaml-cpp (``apt install libgrpc++-dev libprotobuf-dev
protobuf-compiler-grpc libyaml-cpp-dev``); the toolchain question is answered
in ``tools/_toolchain.py`` and the core count in ``tools/_cores.py``, so this
script no longer spells out either one.

The parallel job count defaults to 6, and the build process is pinned to the
six performance cores (see `tools/_cores.py`): this machine has 6 P-cores and
8 E-cores, and spreading a build over the slow ones makes it finish later, not
sooner. `--jobs` overrides the count and `BIG6_CORES` / `BIG6_NO_PIN` override
the pinning. Off Windows the pin is a no-op and `--jobs 6` is simply a cap.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WINDOWS_ROOT = REPO_ROOT / "bridge" / "windows"
DEFAULT_BUILD = WINDOWS_ROOT / "build"

# Toolchain and core policy are shared with every other entry point in this
# repo: tools/_toolchain.py and tools/_cores.py.
sys.path.insert(0, str(REPO_ROOT / "tools"))
from _cores import DEFAULT_JOBS, pin  # noqa: E402
from _toolchain import build_env, default_msys2, find_tools  # noqa: E402


def run(cmd: list[str], cwd: Path | None = None, env: dict | None = None) -> int:
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    proc = subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None, env=env)
    return proc.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=str(DEFAULT_BUILD))
    parser.add_argument("--msys2", default=None,
                        help="MSYS2 root providing the toolchain; Windows only and "
                             "ignored elsewhere (default: %s or $CPU_MSYS2_ROOT)"
                             % default_msys2())
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--clean", action="store_true", help="delete the build directory first")
    parser.add_argument("--test", action="store_true", help="run ctest after building")
    parser.add_argument("--target", action="append", default=None,
                        help="build only this target (repeatable)")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS,
                        help="parallel build jobs (default %(default)s: the 6 big cores, "
                             "never every core)")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    tools = find_tools(args.msys2)
    env = build_env(tools)

    if args.clean and build_dir.exists():
        resolved = build_dir.resolve()
        if WINDOWS_ROOT.resolve() not in resolved.parents:
            raise SystemExit(f"refusing to delete {resolved}: outside {WINDOWS_ROOT}")
        print(f"removing {resolved}")
        shutil.rmtree(resolved)

    build_dir.mkdir(parents=True, exist_ok=True)

    configure = [tools.cmake, "-S", WINDOWS_ROOT, "-B", build_dir, "-G", "Ninja"]
    if tools.cxx is not None:
        # 反斜杠在 -D 里会被 CMake 当成转义（同 nav2/ros2_shim/build.py）。
        configure.append("-DCMAKE_CXX_COMPILER="
                         + str(tools.cxx).replace("\\", "/"))
    configure.append("-DCMAKE_BUILD_TYPE=%s" % args.build_type)
    code = run(configure, env=env)
    if code != 0:
        return code

    print("+ " + pin(), flush=True)
    # `--target` belongs to cmake, before the `--` that hands the rest to the
    # generator: after it, ninja sees `--target` as one of its own flags and
    # answers "unknown tool 'arget'". Without a target, cmake builds everything.
    cmd = [tools.cmake, "--build", build_dir]
    for target in args.target or []:
        cmd += ["--target", target]
    cmd += ["--", "-j", str(args.jobs)]
    code = run(cmd, env=env)
    if code != 0:
        return code

    if args.test:
        if tools.ctest is None:
            raise SystemExit("no ctest next to %s -- pass CMAKE= or install ctest"
                             % tools.cmake)
        code = run([tools.ctest, "--output-on-failure"], cwd=build_dir, env=env)
    return code


if __name__ == "__main__":
    sys.exit(main())
