#!/usr/bin/env python3
"""Check `slam_toolbox-ucrt-binary-archive.patch` against the tree it patches.

Two questions, one command:

  * is the patch still applied - are all four `std::ios::binary` edits, and the
    `PORT (ros2_shim)` markers next to them, still in `third_party/slam_toolbox`?
    This is the same question `nav2/slam_toolbox_port/CMakeLists.txt` asks at
    configure time and refuses to build if the answer is no.

  * is the patch still *accurate* - does the diff between the vendored tree and
    a pristine copy of the upstream release come out exactly as the file says?
    Answering this needs the upstream copy, which is not in the repository, so
    it is `--fetch` (downloads the pinned tag) or `--upstream DIR` (a checkout
    someone already has).

    python nav2/patches/verify_patch.py               # markers only
    python nav2/patches/verify_patch.py --fetch       # markers + diff is exact
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
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR = os.path.join(os.path.dirname(HERE), "third_party", "slam_toolbox")
PATCH = os.path.join(HERE, "slam_toolbox-ucrt-binary-archive.patch")

# The upstream release the vendored tree came from. `package.xml` says the same
# thing; this is the tag the patch is generated against.
UPSTREAM_TAG = "2.8.5"
UPSTREAM_RAW = (
    "https://raw.githubusercontent.com/SteveMacenski/slam_toolbox/{tag}/{path}")

# Every file the patch touches, and the lines this port adds to it. The text is
# matched exactly, so a re-vendor that loses one is reported here rather than
# discovered as an `archive_exception` in a running node.
FILES = {
    "lib/karto_sdk/src/Mapper.cpp": [
        "  // PORT (ros2_shim): binary mode - a text-mode read stops at the first 0x1A.\n",
        "  std::ofstream ofs(filename.c_str(), std::ios::binary);\n",
        "  std::ifstream ifs(filename.c_str(), std::ios::binary);\n",
    ],
    "lib/karto_sdk/include/karto_sdk/Karto.h": [
        "    // PORT (ros2_shim): binary mode - a text-mode read stops at the first 0x1A.\n",
        "    std::ofstream ofs(filename.c_str(), std::ios::binary);\n",
        "    std::ifstream ifs(filename.c_str(), std::ios::binary);\n",
    ],
}

# `std::ios::binary` has to be on all four streams - a text-mode *read* is what
# breaks, but a text-mode write is what puts the byte pattern in the file - so
# the count per file is pinned as well.
BINARY_COUNTS = {
    "lib/karto_sdk/src/Mapper.cpp": 2,
    "lib/karto_sdk/include/karto_sdk/Karto.h": 2,
}


def read(path: str) -> str:
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8")
    return text.replace("\r\n", "\n").replace("\r", "\n")


def check_markers() -> bool:
    ok = True
    for rel, needles in FILES.items():
        path = os.path.join(VENDOR, rel)
        if not os.path.isfile(path):
            print("MISSING  %s" % rel)
            ok = False
            continue
        text = read(path)
        for needle in needles:
            if needle not in text:
                print("MISSING  %s: %s" % (rel, needle.rstrip()))
                ok = False
        found = len(re.findall(r"std::ios::binary", text))
        if found != BINARY_COUNTS[rel]:
            print("COUNT    %s: %d uses of std::ios::binary, expected %d"
                  % (rel, found, BINARY_COUNTS[rel]))
            ok = False
    print("markers  %s" % ("present in all %d files" % len(FILES) if ok else "NOT in place"))
    return ok


def regenerate(pristine: str) -> str:
    chunks = []
    for rel in FILES:
        upstream = os.path.join(pristine, rel)
        if not os.path.isfile(upstream):
            raise SystemExit("no such file: %s" % upstream)
        a = read(upstream).splitlines()
        b = read(os.path.join(VENDOR, rel)).splitlines()
        chunks.append("\n".join(difflib.unified_diff(
            a, b, fromfile="a/" + rel, tofile="b/" + rel, lineterm="", n=3)) + "\n")
    return "\n".join(chunks)


def fetch(pristine: str) -> None:
    for rel in FILES:
        url = UPSTREAM_RAW.format(tag=UPSTREAM_TAG, path=rel)
        target = os.path.join(pristine, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        print("fetching %s" % url)
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        with open(target, "wb") as handle:
            handle.write(data)


def check_accuracy(pristine: str) -> bool:
    expected = read(PATCH)
    actual = regenerate(pristine)
    if expected == actual:
        print("patch    the file matches the vendored tree against %s exactly" % UPSTREAM_TAG)
        return True
    print("patch    DIFFERS from the vendored tree against %s:" % UPSTREAM_TAG)
    for line in difflib.unified_diff(
            expected.splitlines(), actual.splitlines(),
            fromfile="patch file", tofile="regenerated", lineterm="", n=2):
        print("    " + line)
    return False


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
        print("(not checked against upstream: pass --fetch or --upstream DIR; see README.md)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())