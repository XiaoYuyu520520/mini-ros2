#!/usr/bin/env python3
"""List includes under a source root, optionally filtered by prefix."""
import os, re, sys
from collections import Counter

INC_RE = re.compile(r"#include\s*[<\"]([^>\"]+)[>\"]")
SKIP = {".git", "build", "__pycache__"}
CODE_EXT = (".h", ".hpp", ".c", ".cc", ".cpp", ".hxx")


def scan(root, prefixes):
    counter = Counter()
    where = {}
    for r, d, fs in os.walk(root):
        d[:] = [x for x in d if x not in SKIP]
        for f in fs:
            if not f.endswith(CODE_EXT):
                continue
            p = os.path.join(r, f)
            rel = os.path.relpath(p, root)
            try:
                t = open(p, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            for m in INC_RE.finditer(t):
                inc = m.group(1)
                if prefixes and not inc.startswith(tuple(prefixes)):
                    continue
                counter[inc] += 1
                where.setdefault(inc, set()).add(rel)
    return counter, where


def main():
    root = sys.argv[1]
    prefixes = sys.argv[2].split(",") if len(sys.argv) > 2 and sys.argv[2] else []
    show_where = "--where" in sys.argv
    counter, where = scan(root, prefixes)
    if not counter:
        print("(no matches under %s for %s)" % (root, prefixes))
        return
    for k in sorted(counter):
        line = "%4d  %s" % (counter[k], k)
        if show_where:
            line += "   <- " + ", ".join(sorted(where[k])[:3])
        print(line)


if __name__ == "__main__":
    main()
