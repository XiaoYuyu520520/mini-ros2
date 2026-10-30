#!/usr/bin/env python3
"""Keep the shape of the tree and the documents that describe it in agreement.

The repo explains itself layer by layer: the root README, one per sub-project, and
one for every directory that has rules of its own (18 of them today). That only
works while the layers stay true, so this checks the three kinds of drift that
have actually happened here:

* **a directory a newcomer lands in with no README of its own.** The layout was
  described at the top level and in three sub-project READMEs, and
  `nav2/ros2_shim/` - 909 files - had none, so "which of `generated/`,
  `python/`, `apps/` is a build product" could only be answered by reading
  `CMakeLists.txt`.

* **a structure map that names a directory which is not there.** The root
  README's tree blocks are the map; a directory renamed or never created shows
  up as a path that does not resolve.

* **an entry point quoted in a doc that has been renamed.** The docs tell you to
  run `python tools/run_ctest.py`, `bridge/scripts/build_bridge.py`, the four
  `bridge/linux/*.sh` - if one of those moves, the instruction is wrong until
  somebody notices.

`REQUIRED_READMES` and `REQUIRED_FILES` below are the contract, not a scan: a new
top-level area means adding a line here (and writing its README), which is the
point - the list is short enough to read in one go.

    python tools/check_structure.py            # check
    python tools/check_structure.py --list     # print the contract
    python tools/check_structure.py --quiet

Exit code is 1 when something is wrong, 0 otherwise.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Directories a reader is expected to land in, each of which has to say what it
# is. Build trees, `.scratch`'s evidence buckets, generated trees and the
# vendored upstream checkouts are deliberately absent: they are either products,
# covered by a parent, or somebody else's layout.
REQUIRED_READMES = [
    ".",
    ".scratch",
    "bridge",
    "bridge/docs",
    "bridge/linux",
    "bridge/scripts",
    "bridge/tests",
    "bridge/ubuntu/ros2_bridge_sim",
    "bridge/windows",
    "gRPC/ros2_gRPC_sdk",
    "nav2",
    "nav2/patches",
    "nav2/ros2_shim",
    "nav2/ros2_shim/python",
    "nav2/shm_transport",
    "nav2/slam_toolbox_port",
    "nav2/third_party",
    "tools",
]

# The entry points the docs and the CI actually invoke. The point is not that
# these files are special, it is that the commands written down still resolve.
REQUIRED_FILES = [
    ".github/workflows/cross-platform.yml",
    "bridge/linux/bridge_env.sh",
    "bridge/linux/ros2.sh",
    "bridge/linux/ros2_bridge.sh",
    "bridge/linux/wait_bridge_ready.sh",
    "bridge/scripts/build_bridge.py",
    "bridge/scripts/run_amcl_demo.py",
    "bridge/scripts/run_nav2_docking_route.py",
    "bridge/scripts/run_nav2_safety.py",
    "bridge/scripts/run_nav2_stack.py",
    "bridge/scripts/run_slam_demo.py",
    "bridge/scripts/save_map.py",
    "bridge/windows/bridge_env.bat",
    "bridge/windows/ros2.bat",
    "bridge/windows/ros2.ps1",
    "bridge/windows/ros2_bridge.bat",
    "bridge/windows/ros2_shell.bat",
    "bridge/windows/wait_bridge_ready.bat",
    # The car's workspace. `BUILD.md` 3.4, `code/Desktop/racecar/README.md` and
    # the `racecar-build` step of `tools/check_cross_platform.py` all name these
    # two, and that step is what runs the second one - so a rename that leaves
    # the gate pointing at nothing has to fail here first.
    "code/Desktop/racecar/tools/check_mission_scripts.py",
    "code/Desktop/racecar/tools/racecar_ctl.py",
    "gRPC/ros2_gRPC_sdk/scripts/build.py",
    "nav2/patches/verify_patch.py",
    "nav2/ros2_shim/build.py",
    "nav2/ros2_shim/generate.ps1",
    "nav2/shm_transport/build.py",
    "tools/_cores.py",
    "tools/_toolchain.py",
    "tools/check_cross_platform.py",
    "tools/check_docs.py",
    "tools/check_structure.py",
    "tools/ctest_stress.py",
    "tools/find_scratch_refs.py",
    "tools/run_ctest.py",
]

# The structure map: the fenced blocks under this heading in the root README, one
# per top-level area, each written as a box-drawing tree.
MAP_HEADING = "## 目录结构"
MAP_DOC = "README.md"

# Directories a build creates, so their absence in a fresh checkout is normal.
MAY_BE_ABSENT = {"build", "bin", "obj"}

ENTRY_RE = re.compile(r"^(?P<prefix>[│ ]*)[├└]─\s+(?P<name>\S+)")
HEADING_RE = re.compile(r"^#{1,6}\s")


def log(message: str) -> None:
    print(message, flush=True)


def map_blocks(lines: list[str]) -> list[list[str]]:
    """The fenced blocks of the structure map, in order."""
    try:
        start = next(i for i, line in enumerate(lines) if line.strip() == MAP_HEADING)
    except StopIteration:
        return []
    blocks: list[list[str]] = []
    current: list[str] | None = None
    for line in lines[start + 1:]:
        if HEADING_RE.match(line):
            break
        if line.lstrip().startswith("```"):
            if current is None:
                current = []
            else:
                blocks.append(current)
                current = None
            continue
        if current is not None:
            current.append(line)
    return blocks


def map_problems() -> list[str]:
    """Directories the structure map names but the tree does not have."""
    doc = REPO_ROOT / MAP_DOC
    if not doc.is_file():
        return ["%s is missing" % MAP_DOC]
    lines = doc.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    problems: list[str] = []
    for block in map_blocks(lines):
        root = ""
        stack: list[str] = []
        first = True
        for line in block:
            if not line.strip():
                continue
            match = ENTRY_RE.match(line)
            if match is None:
                # Only the block's first line is a path: it names the area the
                # block draws (`bridge/`, `gRPC/ros2_gRPC_sdk/`, `nav2/`), or
                # `CPU/`, which is the repo root under another name. Everything
                # else that is not an entry is an annotation - an entry's text
                # wrapped onto the next line (`│   geometry2、ompl …`), which
                # would otherwise be read as a path made of box characters.
                if first:
                    name = line.strip().split()[0]
                    root = name.rstrip("/") if (REPO_ROOT / name.rstrip("/")).is_dir() else ""
                    stack = []
                first = False
                continue
            first = False
            depth = len(match.group("prefix")) // 3
            name = match.group("name")
            stack = stack[:depth]
            if "*" in name or not name.endswith("/"):
                # A glob, or a file: no directory to resolve, and no child of
                # its own to nest under.
                continue
            path = "/".join(part for part in [root, *stack, name.rstrip("/")] if part)
            stack.append(name.rstrip("/"))
            if Path(name.rstrip("/")).name in MAY_BE_ABSENT:
                continue
            if not (REPO_ROOT / path).is_dir():
                problems.append("%s: %s does not exist" % (MAP_DOC, path))
    return problems


def readme_problems() -> list[str]:
    return ["%s/ has no README.md" % d for d in REQUIRED_READMES
            if not (REPO_ROOT / d / "README.md").is_file()]


def file_problems() -> list[str]:
    return ["%s is missing" % f for f in REQUIRED_FILES
            if not (REPO_ROOT / f).is_file()]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list", action="store_true", help="print the contract and exit")
    parser.add_argument("--quiet", action="store_true", help="print only problems")
    args = parser.parse_args()

    if args.list:
        log("%d directory(ies) that must carry a README:" % len(REQUIRED_READMES))
        for d in REQUIRED_READMES:
            log("    %s" % d)
        log("%d entry point(s) that must exist:" % len(REQUIRED_FILES))
        for f in REQUIRED_FILES:
            log("    %s" % f)
        return 0

    problems = readme_problems() + file_problems() + map_problems()
    if problems:
        log("structure problems (%d):" % len(problems))
        for problem in problems:
            log("    %s" % problem)
        return 1
    if not args.quiet:
        log("structure ok: %d README(s), %d entry point(s), "
            "structure map resolves" % (len(REQUIRED_READMES), len(REQUIRED_FILES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
