#!/usr/bin/env python3
"""Lint the first-party Markdown for the things that have actually broken it.

These documents are not edited by hand in this repo -- they are patched by
scripts, on Windows, and read again on the Ubuntu side -- so the same three
defects keep coming back:

* CRLF creeping in. Every `.md` here is LF-only, and a stray `\\r` shows up as a
  visible box in `bridge/docs/*.md` when it is a whole line ending. (`.bat` files
  are the opposite: CRLF, always.)
* A missing UTF-8 BOM on a file that is full of Chinese. Without it Windows
  tools guess GBK and the file turns into mojibake. Reported as a warning, since
  the two older first-party READMEs are BOM-less and have been fine.
* An odd number of ``` fences, which silently swallows everything after it.

    python tools/check_docs.py                      # every first-party .md
    python tools/check_docs.py bridge/docs/03-validation.md
    python tools/check_docs.py --all                # ... including .scratch
    python tools/check_docs.py --quiet

Exit code is 1 when something is wrong, 0 when the only remarks are warnings.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Upstream sources and build output have their own conventions; they are not ours
# to lint. `attic` is the archived half of `.scratch` (see .scratch/README.md).
# `rubblish` is the archive under `code/Desktop/`: session transcripts and the
# robot-side files that are neither mapping nor navigation, moved there verbatim.
# Its Markdown records what was moved rather than documenting this repo -- the
# transcripts alone carry hundreds of unpaired fences. The racecar tree next to
# it *is* linted, so the docs the workspace actually reads stay in shape.
SKIP_DIRS = {".git", ".pytest_cache", "__pycache__", "build", "third_party",
             "nav2_extracted", "dl", "attic", "generated", "pymessages",
             "generated_nav2", "rubblish"}
BAD_BYTES = sorted(set(range(0x00, 0x09)) | {0x0b, 0x0c} | set(range(0x0e, 0x20)))


def find_docs(root: Path, include_scratch: bool) -> list[Path]:
    out: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames
                             if d not in SKIP_DIRS
                             and (include_scratch or not d.startswith(".")))
        for name in sorted(filenames):
            if name.lower().endswith(".md"):
                out.append(Path(dirpath) / name)
    return out


def check(path: Path) -> tuple[list[str], list[str]]:
    """Return (errors, warnings) for one file."""
    errors: list[str] = []
    warnings: list[str] = []
    raw = path.read_bytes()

    if raw.count(b"\r"):
        errors.append("%d CR byte(s) -- .md here is LF-only" % raw.count(b"\r"))
    bom = raw[:3] == b"\xef\xbb\xbf"
    body = raw[3:] if bom else raw
    if any(b >= 0x80 for b in body) and not bom:
        warnings.append("no UTF-8 BOM on a file with non-ASCII text")
    bad = sorted({b for b in raw if b in BAD_BYTES})
    if bad:
        errors.append("control byte(s): " + ", ".join("0x%02X" % b for b in bad))
    text = body.decode("utf-8", "replace")
    fences = sum(1 for line in text.splitlines() if line.lstrip().startswith("```"))
    if fences % 2:
        errors.append("odd number of ``` fences (%d) -- the rest of the file is inside one" % fences)
    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="*", help="files or directories (default: the repo root)")
    parser.add_argument("--all", action="store_true",
                        help="do not skip dot-directories such as .scratch")
    parser.add_argument("--quiet", action="store_true", help="print only the files with remarks")
    args = parser.parse_args()

    targets: list[Path] = []
    for spec in args.paths or [str(REPO_ROOT)]:
        path = Path(spec)
        if not path.is_absolute():
            path = (Path.cwd() / path).resolve()
        if path.is_dir():
            targets += find_docs(path, args.all)
        elif path.is_file():
            targets.append(path)
        else:
            raise SystemExit("no such file or directory: %s" % path)

    bad = 0
    warned = 0
    for path in sorted(set(targets)):
        errors, warnings = check(path)
        if errors:
            bad += 1
        if warnings:
            warned += 1
        if args.quiet and not (errors or warnings):
            continue
        rel = path.relative_to(REPO_ROOT) if str(path).startswith(str(REPO_ROOT)) else path
        print("%-58s %s" % (rel, "ok" if not (errors or warnings) else
                            " ".join(["ERR"] * len(errors) + ["warn"] * len(warnings))))
        for line in errors:
            print("     error: %s" % line)
        for line in warnings:
            print("     warn : %s" % line)

    print("\n%d file(s), %d with errors, %d with warnings" % (len(set(targets)), bad, warned))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())