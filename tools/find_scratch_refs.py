#!/usr/bin/env python3
"""List every `.scratch/...` path that a document or a source file refers to.

`.scratch/` is the evidence store behind `bridge/docs/`: 03-validation.md alone
cites over a hundred paths in there, and a path that stops resolving turns
"evidence 9" into a dead link. So run this before moving or deleting anything
inside `.scratch/`.

    python tools/find_scratch_refs.py                 # refs, with who cites them
    python tools/find_scratch_refs.py --keep-list     # the keep-set the 2026-09-17
                                                      # cleanup used
    python tools/find_scratch_refs.py --check         # exit 1 if a ref is gone

`--check` accepts a prefix match: docs write `.scratch\\e2e_map.posegraph`, and a
literal reading of that also yields the bare name `e2e_map`, which is not itself a
file. A reference counts as resolved when it exists, or when some entry in
`.scratch/` starts with it.

The keep rule for `--keep-list` is deliberately generous: an entry is kept when
its path is referenced *or* when its name (or its stem, if at least 4 characters
long) appears anywhere in one of the referencing files. That is what kept 228 of
3892 top-level entries in place during the 2026-09-17 tidy-up.
"""
from __future__ import annotations

import argparse
import collections
import glob
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRATCH = REPO_ROOT / ".scratch"

# Everything that quotes a `.scratch/...` path: the bridge docs, the two READMEs
# and the one source comment (graph.hpp). Discovered the slow way.
REFERRING = [
    "README.md",
    "bridge/README.md",
    "bridge/docs/01-architecture.md",
    "bridge/docs/02-running.md",
    "bridge/docs/03-validation.md",
    "bridge/docs/04-gaps.md",
    "bridge/docs/05-audit.md",
    "bridge/ubuntu/ros2_bridge_sim/README.md",
    "nav2/ros2_shim/include/ros2_shim/graph.hpp",
]

PATTERN = re.compile(r"\.scratch[\\/]([A-Za-z0-9_\-\.\\/]*)")


def load_referring() -> list[tuple[str, str]]:
    out = []
    for rel in REFERRING:
        path = REPO_ROOT / rel
        if path.is_file():
            out.append((rel, path.read_text(encoding="utf-8-sig", errors="replace")))
    return out


def references(texts: list[tuple[str, str]]) -> dict[str, set[str]]:
    refs: dict[str, set[str]] = collections.defaultdict(set)
    for rel, text in texts:
        for match in PATTERN.finditer(text):
            ref = match.group(1).rstrip("\\/")
            if ref:
                refs[ref].add(rel)
    return refs


def resolves(ref: str) -> bool:
    return os.path.exists(SCRATCH / ref) or bool(glob.glob(str(SCRATCH / (ref + "*"))))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--keep-list", action="store_true",
                        help="print the entries a cleanup must leave alone")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when a referenced path no longer resolves")
    args = parser.parse_args()

    texts = load_referring()
    refs = references(texts)

    if args.keep_list:
        entries = os.listdir(SCRATCH) if SCRATCH.is_dir() else []
        keep = set()
        for entry in entries:
            if entry in refs:
                keep.add(entry)
                continue
            stem = os.path.splitext(entry)[0]
            for _, text in texts:
                if entry in text or (len(stem) >= 4 and stem in text):
                    keep.add(entry)
                    break
        for entry in sorted(keep):
            print(entry)
        print("# %d of %d top-level entries are referenced by name" % (len(keep), len(entries)),
              file=sys.stderr)
        return 0

    missing = []
    for ref in sorted(refs):
        if not resolves(ref):
            missing.append(ref)
    if not args.check:
        print("%d distinct .scratch paths referenced by %d file(s)"
              % (len(refs), len(texts)))
        for ref in sorted(refs):
            print("  %-42s <- %s" % (ref, ", ".join(sorted(refs[ref]))))
        print()
    print("%d referenced path(s) do not resolve%s"
          % (len(missing), (": " + ", ".join(missing)) if missing else ""))
    return 1 if (args.check and missing) else 0


if __name__ == "__main__":
    sys.exit(main())