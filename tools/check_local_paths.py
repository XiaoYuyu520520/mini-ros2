# -*- coding: utf-8 -*-
"""Report machine-local paths in the first-party Markdown.

A published document has to say *what* to run, not where it happened to live
on one development machine. The paths that leaked in were always the same
few shapes: this checkout's root, the Python virtualenv, the MSYS2 prefix,
the developer's home directory.

The check is a report, not an error: `bridge/docs/03-validation.md` is a lab
notebook whose command lines are evidence, and it keeps the real paths in the
one sentence that defines the placeholders. That sentence is why the file has
an exemption below.

    python tools/check_local_paths.py            # every first-party .md
    python tools/check_local_paths.py --quiet
    python tools/check_local_paths.py docs/      # only that tree

Exit code is 1 when a machine-local path is found outside the exemptions.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

#: Upstream sources, build output and archives have their own conventions.
SKIP_DIRS = {".git", ".pytest_cache", "__pycache__", "build", "third_party",
             "nav2_extracted", "dl", "attic", "generated", "pymessages",
             "generated_nav2", "rubblish"}

#: The paths that must not appear. Each is written so that the check works on
#: both spellings (backslash and slash) without a regex.
BACKSLASH = chr(92)
#: `/home/<user>` on its own is deliberately *not* listed: it is every Ubuntu
#: user's home, and `ubuntu_ros2_setup.sh` legitimately names `/home/ubuntu`.
#: The leaked shape was the specific checkout path inside it.
PATTERNS = [
    "E:" + BACKSLASH + "venvs",
    "E:/venvs",
    "D:" + BACKSLASH + "WindowsServer2025_1",
    "D:/WindowsServer2025_1",
    "C:" + BACKSLASH + "Users" + BACKSLASH + "Administrator",
    "~/code/CPU",
    "/home/ubuntu/code/CPU",
]

#: Scripts ship inside the archives exactly like the Markdown does, so a path
#: copied out of one developer's shell belongs in the same report. For scripts
#: the toolchain prefix and the virtualenv are *legitimate* defaults - both
#: are overridable and both are documented - but the checkout root and the
#: user's home directory are not.
SCRIPT_PATTERNS = [
    "E:" + BACKSLASH + "DesktopData" + BACKSLASH + "app" + BACKSLASH + "code" + BACKSLASH + "CPU",
    "E:/DesktopData/app/code/CPU",
    "C:" + BACKSLASH + "Users" + BACKSLASH + "Administrator",
    "~/code/CPU",
]

#: The scripts that end up in an archive: `start/` and the two `bridge/`
#: halves. `bridge/scripts/` is the operator toolbox - it is packaged too, but
#: its `.py` files carry toolchain defaults, so only the shell scripts there
#: are scanned.
SCRIPT_SUFFIXES = (".ps1", ".bat", ".sh")
SCRIPT_DIRS = ("start", "bridge/windows", "bridge/linux", "bridge/scripts")

#: Files that may name the real paths, because they are the ones that explain
#: the placeholders. Every entry needs a reason.
EXEMPTIONS = {
    "bridge/docs/03-validation.md":
        "the note at the top defines what the placeholders stand for",
}


def find_docs(root: Path) -> list[Path]:
    out: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for name in sorted(filenames):
            if name.lower().endswith((".md",) + SCRIPT_SUFFIXES):
                out.append(Path(dirpath) / name)
    return out


def _is_script(relative: Path) -> bool:
    """A shell / PowerShell entry point that travels in an archive."""
    if relative.suffix.lower() not in SCRIPT_SUFFIXES:
        return False
    parts = relative.as_posix().split("/")
    return len(parts) > 1 and "/".join(parts[:2]) in SCRIPT_DIRS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="*", help="files or directories (default: the repo root)")
    parser.add_argument("--quiet", action="store_true", help="print only the files with hits")
    args = parser.parse_args()

    targets: list[Path] = []
    for spec in args.paths or [str(REPO_ROOT)]:
        path = Path(spec)
        if not path.is_absolute():
            path = (Path.cwd() / path).resolve()
        if path.is_dir():
            targets += find_docs(path)
        elif path.is_file():
            targets.append(path)
        else:
            raise SystemExit("no such file or directory: %s" % path)

    bad = 0
    for path in sorted(set(targets)):
        try:
            relative = path.relative_to(REPO_ROOT)
        except ValueError:
            relative = path
        key = relative.as_posix()
        # Session transcripts at the repo root are scratch, not first-party
        # docs: they are untracked and never published.
        if relative.name.startswith("session-") and relative.parent == Path("."):
            continue
        is_script = _is_script(relative)
        if not is_script and relative.suffix.lower() != ".md":
            continue
        if key in EXEMPTIONS:
            continue
        patterns = SCRIPT_PATTERNS if is_script else PATTERNS
        body = path.read_bytes().decode("utf-8", "replace")
        hits = []
        for number, line in enumerate(body.split("\n"), 1):
            for pattern in patterns:
                if pattern in line:
                    hits.append((number, pattern, line.strip()))
                    break
        if not hits:
            if not args.quiet:
                print("%-58s ok" % relative)
            continue
        bad += 1
        print("%-58s %d hit(s)" % (relative, len(hits)))
        for number, pattern, line in hits[:5]:
            print("   line %d  %s" % (number, pattern))
            print("      %s" % line[:160])
    print("\n%d file(s) with machine-local paths" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())