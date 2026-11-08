#!/usr/bin/env python3
"""Hook: refuse a Markdown write that leaves the file outside the repo's policy.

`check_docs.py` is the authority on what a `.md` here looks like (UTF-8, LF, a
BOM when it carries non-ASCII, balanced fences); this script only decides
*which* file to look at and turns the answer into a permission decision.

It runs as a Claude Code PreToolUse hook on Edit / Write / MultiEdit: the tool
call arrives as JSON on stdin. Only `.md` targets are checked, and the target is
checked as it *would* be after the edit (Edit applies its replacement in
memory first), so a write that would introduce CR bytes, a stray control byte
or an unpaired fence is refused before it touches the disk.

    echo '{"tool_name":"Edit","tool_input":{"file_path":"docs/index.md"}}' | \\
        py -3 -I tools/hooks/check_md_write.py

Exit code 0 always (a hook that fails is a hook that fails silently). The
decision is printed on stdout as JSON:

    {"hookSpecificOutput": {"hookEventName": "PreToolUse",
                            "permissionDecision": "deny" | "allow",
                            "permissionDecisionReason": "..."},
     "systemMessage": "..."}

`deny` only when the file would fail `check_docs.py` (its exit code is 1 for
errors, 0 for warnings alone). A missing BOM is a warning in this repo, so it
is reported but does not block.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import check_docs  # noqa: E402  (the linter itself, not a copy of its rules)

BOM = b"\xef\xbb\xbf"


def _target(payload: dict) -> Path | None:
    tool_input = payload.get("tool_input") or {}
    target = tool_input.get("file_path") or ""
    if not target or Path(target).suffix.lower() != ".md":
        return None
    path = Path(target)
    if not path.is_absolute():
        path = (REPO_ROOT / path).resolve()
    return path


def _after_edit(path: Path, payload: dict) -> bytes | None:
    """The bytes the file would have after the edit, or None when unknown."""
    tool = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}
    if tool == "Write":
        content = tool_input.get("content", "")
        return content.encode("utf-8")
    if tool != "Edit":
        return None
    try:
        raw = path.read_bytes()
    except OSError:
        return None
    body = raw[3:] if raw[:3] == BOM else raw
    text = body.decode("utf-8", "replace")
    old = tool_input.get("old_string", "")
    new = tool_input.get("new_string", "")
    if not old:
        return None
    if tool_input.get("replace_all"):
        if old not in text:
            return None
        text = text.replace(old, new)
    else:
        if text.count(old) != 1:
            return None
        text = text.replace(old, new)
    out = text.encode("utf-8")
    return (BOM + out) if raw[:3] == BOM else out


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    path = _target(payload)
    if path is None:
        return 0
    raw = _after_edit(path, payload)
    if raw is None:
        return 0

    errors, warnings = check_docs.check_bytes(raw)
    if not errors and not warnings:
        return 0

    try:
        shown = path.relative_to(REPO_ROOT)
    except ValueError:
        shown = path
    answer = {"hookSpecificOutput": {"hookEventName": "PreToolUse"}}
    if errors:
        answer["hookSpecificOutput"]["permissionDecision"] = "deny"
        answer["hookSpecificOutput"]["permissionDecisionReason"] = (
            "this write leaves %s outside the .md policy: %s (tools/check_docs.py)"
            % (shown, "; ".join(errors)))
    else:
        answer["hookSpecificOutput"]["permissionDecision"] = "allow"
        answer["hookSpecificOutput"]["permissionDecisionReason"] = (
            "the .md policy is met; note: %s" % "; ".join(warnings))
    answer["systemMessage"] = "[check_md_write] %s: %s" % (
        shown, "; ".join(errors + warnings))
    print(json.dumps(answer, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
