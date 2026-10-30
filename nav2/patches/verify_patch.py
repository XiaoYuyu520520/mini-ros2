#!/usr/bin/env python3
"""Check the patches in this directory against the tree they patch.

Three questions, one command:

  * is every patch still applied - are the edits, and the `PORT (ros2_shim)`
    markers next to them, still in `third_party/slam_toolbox`? This is the same
    question `nav2/slam_toolbox_port/CMakeLists.txt` asks at configure time and
    refuses to build if the answer is no.

  * is every *edit* covered by a patch? A change that lives in the vendored tree
    without a patch is the failure this directory exists to prevent, so a file
    that carries a marker and no patch entry is reported here.

  * is each patch still *accurate* - does the diff between the vendored tree and
    a pristine copy of the upstream release come out exactly as the file says?
    Answering this needs the upstream copy, which is not in the repository, so
    it is `--fetch` (downloads the pinned tag) or `--upstream DIR` (a checkout
    someone already has).

    python nav2/patches/verify_patch.py               # markers only
    python nav2/patches/verify_patch.py --fetch       # markers + diffs are exact
    python nav2/patches/verify_patch.py --upstream C:\\src\\slam_toolbox

Exit status is 0 when everything asked for holds.
"""
from __future__ import annotations

import argparse
import difflib
import os
import re
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR = os.path.join(os.path.dirname(HERE), "third_party", "slam_toolbox")

# The upstream release the vendored tree came from. `package.xml` says the same
# thing; this is the tag the patches are generated against.
UPSTREAM_TAG = "2.8.5"
UPSTREAM_RAW = (
    "https://raw.githubusercontent.com/SteveMacenski/slam_toolbox/{tag}/{path}")

# Every file this port edits, the lines it adds to it, and how many times each
# marker has to appear. The text is matched exactly, so a re-vendor that loses
# one is reported here rather than discovered as an `archive_exception` - or as
# a `std::terminate`, the other failure mode below - in a running node.
FILES = {
    "lib/karto_sdk/src/Mapper.cpp": {
        "lines": [
            "  // PORT (ros2_shim): binary mode - a text-mode read stops at the first 0x1A.\n",
            "  std::ofstream ofs(filename.c_str(), std::ios::binary);\n",
            "  std::ifstream ifs(filename.c_str(), std::ios::binary);\n",
        ],
        # `std::ios::binary` has to be on all four streams - a text-mode *read*
        # is what breaks, but a text-mode write is what puts the byte pattern in
        # the file - and one marker per edit is what makes them greppable.
        "counts": {"std::ios::binary": 2, "PORT (ros2_shim)": 2},
    },
    "lib/karto_sdk/include/karto_sdk/Karto.h": {
        "lines": [
            "    // PORT (ros2_shim): binary mode - a text-mode read stops at the first 0x1A.\n",
            "    std::ofstream ofs(filename.c_str(), std::ios::binary);\n",
            "    std::ifstream ifs(filename.c_str(), std::ios::binary);\n",
        ],
        "counts": {"std::ios::binary": 2, "PORT (ros2_shim)": 2},
    },
    # The third edit is not about Karto at all: upstream's destructor is empty,
    # so the base destructor joins a thread that keeps locking a mutex which
    # belongs to the derived class and is already destroyed by then. That window
    # aborts the process on glibc and does nothing on Windows, which makes it
    # the edit most likely to be dropped by a re-vendor without anyone noticing.
    "include/slam_toolbox/slam_toolbox_sync.hpp": {
        "lines": [
            "  // PORT (ros2_shim): interrupt and join the threads here, before the base\n",
            "    threads_.clear();\n",
        ],
        "counts": {"PORT (ros2_shim)": 1, "thread->join()": 1},
    },
}

# One patch per concern, each generated against the same upstream tag and
# covering a disjoint set of files. Together they have to cover every file in
# FILES: `check_markers` reports a file that carries an edit and no patch.
PATCHES = {
    "slam_toolbox-ucrt-binary-archive.patch": [
        "lib/karto_sdk/src/Mapper.cpp",
        "lib/karto_sdk/include/karto_sdk/Karto.h",
    ],
    "slam_toolbox-thread-shutdown.patch": [
        "include/slam_toolbox/slam_toolbox_sync.hpp",
    ],
}


def read(path: str) -> str:
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8")
    return text.replace("\r\n", "\n").replace("\r", "\n")


def check_markers() -> bool:
    ok = True
    for rel, spec in FILES.items():
        path = os.path.join(VENDOR, rel)
        if not os.path.isfile(path):
            print("MISSING  %s" % rel)
            ok = False
            continue
        text = read(path)
        for needle in spec["lines"]:
            if needle not in text:
                print("MISSING  %s: %s" % (rel, needle.rstrip()))
                ok = False
        for token, expected in spec["counts"].items():
            found = len(re.findall(re.escape(token), text))
            if found != expected:
                print("COUNT    %s: %d uses of %s, expected %d"
                      % (rel, found, token, expected))
                ok = False
    print("markers  %s" % ("present in all %d files" % len(FILES) if ok else "NOT in place"))

    # An edit with no patch behind it is the whole reason this directory exists.
    covered = [rel for rels in PATCHES.values() for rel in rels]
    uncovered = sorted(set(FILES) - set(covered))
    duplicated = sorted(rel for rel in set(covered) if covered.count(rel) > 1)
    if uncovered or duplicated:
        print("COVERAGE unregistered: %s; in more than one patch: %s"
              % (uncovered, duplicated))
        ok = False
    else:
        print("coverage all %d edited files are in exactly one of the %d patches"
              % (len(FILES), len(PATCHES)))
    return ok


def regenerate(pristine: str, rels) -> str:
    chunks = []
    for rel in rels:
        upstream = os.path.join(pristine, rel)
        if not os.path.isfile(upstream):
            raise SystemExit("no such file: %s" % upstream)
        a = read(upstream).splitlines()
        b = read(os.path.join(VENDOR, rel)).splitlines()
        chunks.append("\n".join(difflib.unified_diff(
            a, b, fromfile="a/" + rel, tofile="b/" + rel, lineterm="", n=3)) + "\n")
    return "\n".join(chunks)


def download(url: str, attempts: int = 4) -> bytes:
    """`urlopen` with a retry, because raw.githubusercontent.com is not a CDN
    this project controls: a bare connection reset (WinError 10054) says
    nothing about the tag, and the first audit run that used this lost a file
    to exactly that."""
    last = None
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return response.read()
        except Exception as exc:  # noqa: BLE001 - reported, then retried
            last = exc
            print("  attempt %d/%d failed: %s" % (attempt + 1, attempts, exc))
            time.sleep(2.0 * (attempt + 1))
    raise SystemExit("could not fetch %s: %s" % (url, last))


def fetch(pristine: str) -> None:
    for rel in sorted(FILES):
        url = UPSTREAM_RAW.format(tag=UPSTREAM_TAG, path=rel)
        target = os.path.join(pristine, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        print("fetching %s" % url)
        data = download(url)
        with open(target, "wb") as handle:
            handle.write(data)


def check_accuracy(pristine: str) -> bool:
    ok = True
    for name, rels in PATCHES.items():
        expected = read(os.path.join(HERE, name))
        actual = regenerate(pristine, rels)
        if expected == actual:
            print("patch    %s matches the vendored tree against %s exactly"
                  % (name, UPSTREAM_TAG))
            continue
        print("patch    %s DIFFERS from the vendored tree against %s:"
              % (name, UPSTREAM_TAG))
        for line in difflib.unified_diff(
                expected.splitlines(), actual.splitlines(),
                fromfile="patch file", tofile="regenerated", lineterm="", n=2):
            print("    " + line)
        ok = False
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--upstream", default="", help="a slam_toolbox checkout to diff against")
    parser.add_argument("--fetch", action="store_true",
                        help="download the pinned upstream tag instead of --upstream")
    args = parser.parse_args()

    ok = check_markers()
    if args.upstream and args.fetch:
        raise SystemExit("--upstream and --fetch are alternatives")

    if args.upstream:
        ok = check_accuracy(args.upstream) and ok
    elif args.fetch:
        with tempfile.TemporaryDirectory(prefix="slam-upstream-") as tmp:
            fetch(tmp)
            ok = check_accuracy(tmp) and ok
    else:
        print("(diffs not checked against upstream: pass --fetch or --upstream DIR; see README.md)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
