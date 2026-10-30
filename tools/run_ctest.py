#!/usr/bin/env python3
"""Run the tests of one tree, on the six big cores.

There are five test trees in this repo (the "怎么开发" section of the top-level
README lists them) and four of them are ctest trees: shim, shm, bridge, grpc.
The fifth is the Ubuntu simulator's pytest tree, which is in here too so that
"run the tests" is the same command for all five rather than four plus one that
lives in a different paragraph.

`ctest` by hand from the MSYS2 UCRT64 shell works, but the build policy here is
6 jobs on the six P-cores -- and a few of these tests are timing sensitive, so
letting them spread over the eight E-cores is how you get flaky results that do
not reproduce. The pytest tree runs on this machine, not on the robot: the
Ubuntu venv has grpc / numpy / protobuf and no pytest.

    python tools/run_ctest.py                    # shim tree, tests one at a time
    python tools/run_ctest.py shm                # shim shm bridge grpc sim
    python tools/run_ctest.py bridge -j 4
    python tools/run_ctest.py shim -R slam --output-on-failure
    python tools/run_ctest.py sim -x -k watchdog
    python tools/run_ctest.py --list

`grpc` is the one tree whose build entry point puts each platform in its own
subdirectory (`build/<platform-key>`); the directory that is actually tested is
the one holding a `CTestTestfile.cmake`, so a flat `build/` left over from an
older layout is not mistaken for today's build.

Anything after the tree name is forwarded to ctest unchanged, so `-R`, `-j`,
`-V`, `--repeat until-fail:20` and friends keep working. Pinning is inherited by
the test processes; `BIG6_NO_PIN=1` disables it, `BIG6_CORES=` overrides it.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _cores import pin  # noqa: E402
from _toolchain import build_env, find_tools  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]

# Always forward slashes: they are the separator on Linux and CMake, Python
# and Windows all accept them, so one spelling covers both platforms.
TREES = {
    "shim": "nav2/ros2_shim/build",
    "shm": "nav2/shm_transport/build",
    "bridge": "bridge/windows/build",
    "grpc": "gRPC/ros2_gRPC_sdk/build",
    "sim": "bridge/ubuntu/ros2_bridge_sim/tests",
}

# The one tree that is not ctest. Its path is a source directory rather than a
# build directory, so it has no `CTestTestfile.cmake` to look for and the runner
# is `pytest` instead of `ctest`; everything after the tree name is forwarded to
# it the same way.
PYTEST_TREES = {"sim"}

# `gRPC/ros2_gRPC_sdk/scripts/build.py` writes one build tree per platform
# (`build/<platform-key>`) and records them in `build/platforms.yaml`, so for
# that tree "the ctest directory" is a lookup rather than a fixed path. It is
# found by looking for the subdirectory that really holds a
# `CTestTestfile.cmake` instead of by reading the manifest, so `tools/` does not
# grow a YAML dependency of its own. Without this, `run_ctest.py grpc` passed
# only while a flat `build/` from an older layout was still lying around, and
# ran two-day-old binaries out of it.
PER_PLATFORM = {"grpc"}


def per_platform(build_root: Path) -> Path:
    candidates = sorted(
        child for child in build_root.glob("*")
        if child.is_dir() and (child / "CTestTestfile.cmake").is_file())
    if not candidates:
        return build_root
    os_name = "windows" if os.name == "nt" else "linux"
    same_os = [child for child in candidates if child.name.startswith(os_name + "-")]
    if len(same_os) == 1:
        return same_os[0]
    return candidates[0] if len(candidates) == 1 else build_root


def resolve(tree: str) -> Path:
    path = Path(TREES.get(tree, tree))
    if not path.is_absolute():
        path = REPO_ROOT / path
    return per_platform(path) if tree in PER_PLATFORM else path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tree", nargs="?", default="shim",
                        help="short name (%s) or a build directory" % ", ".join(TREES))
    parser.add_argument("--list", action="store_true", help="show the known trees and exit")
    parser.add_argument("--msys2", default=None,
                        help="MSYS2 root, for ctest and the DLLs next to it; "
                             "Windows only (tools/_toolchain.py)")
    parser.add_argument("--jobs", type=int, default=None,
                        help="forwarded as `ctest -j N` (default: ctest's own, i.e. serial; "
                             "the pytest tree ignores it)")
    args, rest = parser.parse_known_args()

    if args.list:
        for name in TREES:
            path = resolve(name)
            try:
                shown = path.relative_to(REPO_ROOT).as_posix()
            except ValueError:
                shown = str(path)
            kind = "pytest" if name in PYTEST_TREES else "ctest"
            print("%-8s %-8s %-46s %s"
                  % (name, kind, shown, "ok" if path.is_dir() else "MISSING"))
        return 0

    tree = resolve(args.tree)
    note = pin()

    if args.tree in PYTEST_TREES:
        if not tree.is_dir():
            raise SystemExit("no such test tree: %s" % tree)
        # The directory goes before the forwarded flags so the printed command
        # reads the way somebody would type it; pytest takes it anywhere.
        argv = [sys.executable, "-m", "pytest", "-q", str(tree)] + rest
        env = None
        print("[run_ctest] %s" % tree, flush=True)
        print("[run_ctest] %s -- %s" % (" ".join(argv[2:]), note), flush=True)
    else:
        if not (tree / "CTestTestfile.cmake").is_file():
            raise SystemExit("not a ctest tree (no CTestTestfile.cmake): %s" % tree)
        tools = find_tools(args.msys2, need_ninja=False)
        if tools.ctest is None:
            raise SystemExit("no ctest next to %s -- pass --msys2, CMAKE= or install ctest"
                             % tools.cmake)
        env = build_env(tools)
        argv = [str(tools.ctest), "--output-on-failure"]
        if args.jobs:
            argv += ["-j", str(args.jobs)]
        argv += rest
        print("[run_ctest] %s" % tree, flush=True)
        print("[run_ctest] ctest %s -- %s" % (" ".join(argv[1:]), note), flush=True)
    began = time.monotonic()
    done = subprocess.run(argv, cwd=str(tree), env=env, capture_output=True,
                          text=True, errors="replace")
    sys.stdout.write(done.stdout)
    sys.stdout.write(done.stderr)
    print("[run_ctest] rc=%d in %.1f s" % (done.returncode, time.monotonic() - began))
    return done.returncode


if __name__ == "__main__":
    sys.exit(main())