#!/usr/bin/env python3
"""Tiny, exact-text file editing helper used to drive this repo from the shell.

The Codex `apply_patch` shim is not usable from PowerShell in this environment
(it forwards the patch through a .bat file, which mangles newlines), so file
writes and exact-text replacements go through this script instead.

Commands
--------
write   --path TARGET [--from-file F]          write/replace a whole file
replace --path TARGET --old-file F --new-file F exact-text replacement

* contents are read as raw UTF-8 (a BOM is tolerated and stripped),
* line endings are normalised to LF,
* a target that already starts with a BOM keeps it. Half this repo's files are
  UTF-8 *with* BOM by policy (every `.md`; `tools/check_docs.py` warns without
  it) and the other half must not have one (every `.py` / `.sh` / `.cpp`), so
  the byte order mark is a property of the *target*, not of this tool: rewriting
  a file must not change which side of that line it is on. It did, which meant
  every `.md` edit through here left a warning behind for `check_docs.py`,
* `replace` fails loudly unless the old text occurs exactly once,
* writes are atomic (temp file + os.replace).
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _read_bytes(args: argparse.Namespace, which: str) -> bytes:
    path = getattr(args, f"{which}_file", "")
    if path:
        with open(path, "rb") as handle:
            return handle.read()
    data = sys.stdin.buffer.read()
    if not data:
        raise SystemExit(f"error: no {which} text supplied (use --{which}-file or stdin)")
    return data


def _normalise(raw: bytes) -> str:
    text = raw.decode("utf-8")
    if text.startswith("\ufeff"):
        text = text[1:]
    return text.replace("\r\n", "\n").replace("\r", "\n")


def _had_bom(target: str) -> bool:
    try:
        with open(target, "rb") as handle:
            return handle.read(3) == b"\xef\xbb\xbf"
    except FileNotFoundError:
        return False


def _write(target: str, text: str, bom: bool = False) -> None:
    target = os.path.abspath(target)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", delete=False,
        dir=os.path.dirname(target), prefix=".fsedit-")
    try:
        with handle:
            handle.write(("\ufeff" if bom else "") + text)
        os.replace(handle.name, target)
    except BaseException:
        os.unlink(handle.name)
        raise
    print(f"wrote {target} ({len(text)} chars, {text.count(chr(10))} lines"
          f"{', BOM kept' if bom else ''})")


def cmd_write(args: argparse.Namespace) -> None:
    # A brand new file gets no BOM: only the target can say whether it wants one.
    _write(args.path, _normalise(_read_bytes(args, "from")), _had_bom(args.path))


def cmd_replace(args: argparse.Namespace) -> None:
    target = os.path.abspath(args.path)
    bom = _had_bom(target)
    with open(target, "rb") as handle:
        text = _normalise(handle.read())
    old = _normalise(_read_bytes(args, "old"))
    new = _normalise(_read_bytes(args, "new"))
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"error: old text occurs {count} times in {args.path}, expected exactly 1")
    _write(target, text.replace(old, new), bom)


def main() -> None:
    parser = argparse.ArgumentParser(description="exact-text file editor")
    sub = parser.add_subparsers(dest="command", required=True)

    write = sub.add_parser("write")
    write.add_argument("--path", required=True)
    write.add_argument("--from-file", default="")
    write.set_defaults(func=cmd_write)

    replace = sub.add_parser("replace")
    replace.add_argument("--path", required=True)
    replace.add_argument("--old-file", required=True)
    replace.add_argument("--new-file", required=True)
    replace.set_defaults(func=cmd_replace)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()