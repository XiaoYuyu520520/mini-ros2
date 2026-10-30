# -*- coding: utf-8 -*-
import io, os, re, glob

sec = re.compile(r"## \u8bc1\u636e (\d+) \u00b7")
pats = ["doc*.py", "w_doc*.py", "patch_docs*.py", "*doc*.py", "w_doc*.md", "*doc*.md"]
files = set()
for pat in pats:
    for base in (".scratch", "."):
        files.update(glob.glob(os.path.join(base, pat)))
for p in sorted(files):
    if os.path.isdir(p):
        continue
    t = io.open(p, encoding="utf-8", errors="replace").read()
    found = sorted(set(int(m) for m in sec.findall(t)))
    if found or "\u8bc1\u636e" in t:
        print("%-40s %8d bytes  sections=%s" % (p, len(t), found if found else "-"))