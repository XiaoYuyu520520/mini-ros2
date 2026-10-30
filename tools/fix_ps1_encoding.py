"""Give the start/ PowerShell files the encoding PowerShell actually needs.

Windows PowerShell 5.1 reads a `.ps1` as ANSI unless it starts with a UTF-8
BOM, so a UTF-8 file with Chinese in it gets the Chinese mangled - and a
mangled *filename* means "the file does not exist". PowerShell 7 defaults to
UTF-8, but a file that has to work on both needs the BOM.

CRLF as well: these are Windows scripts.
"""

from __future__ import annotations

import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    root = Path(argv[0]) if argv else Path("start")
    if not root.is_dir():
        print(f"{root} is not a directory", file=sys.stderr)
        return 1
    changed = 0
    for path in sorted(root.glob("*.ps1")):
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig")  # drop an existing BOM if there is one
        text = text.replace("\r\n", "\n").replace("\n", "\r\n")
        path.write_bytes(b"\xef\xbb\xbf" + text.encode("utf-8"))
        print(f"UTF-8 BOM + CRLF: {path}")
        changed += 1
    print(f"{changed} file(s) rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
