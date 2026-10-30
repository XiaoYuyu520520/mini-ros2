# -*- coding: utf-8 -*-
"""Decode a mission log whatever its bytes are.

The first batches let the clients' stdout default to the console code page, and
the clients that log in Chinese were written as cp936; the ones that log in
English are pure ASCII, which is valid UTF-8. Try UTF-8 strictly, fall back to
cp936, and say which one won so the numbers in the documents can be trusted.
"""
import io
import os
import re
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ACCEPT = re.compile("Goal accepted|\u76ee\u6807\u5df2\u63a5\u53d7|\u76ee\u6807\u5df2\u88ab\u63a5\u53d7")
RESULT = re.compile("Navigation result|\u5bfc\u822a\u7ed3\u679c")
FEEDBACK = re.compile("Distance to target|\u5230\u76ee\u6807\u7684\u8ddd\u79bb")
EXIT = re.compile(r"^-- exit (-?\d+) after ([\d.]+) s")
STAMP = re.compile(r"\[INFO\] \[(\d+\.\d+)\]")


def read(path):
    raw = open(path, "rb").read()
    try:
        return raw.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        return raw.decode("cp936", "replace"), "cp936"


out = []
for path in sorted(glob.glob(os.path.join(HERE, "e2e_batch_s*.log"))):
    tag = os.path.basename(path)[len("e2e_batch_"):-4]
    text, codec = read(path)
    lines = text.split("\n")
    header = lines[0].lstrip("# ").strip() if lines else ""
    accepted = [STAMP.search(l).group(1) for l in lines if ACCEPT.search(l) and STAMP.search(l)]
    feedback = sum(1 for l in lines if FEEDBACK.search(l))
    results = []
    for l in lines:
        if not RESULT.search(l):
            continue
        stamp = STAMP.search(l)
        code = re.search(r"error_code=(-?\d+)", l)
        msg = re.search(r"error_msg='([^']*)'", l)
        wps = re.findall(r"waypoint_status=(\d+), waypoint_index=(\d+)", l)
        counts = {}
        for status, _ in wps:
            counts[status] = counts.get(status, 0) + 1
        results.append((stamp.group(1) if stamp else "?",
                        code.group(1) if code else "?",
                        ",".join("%s=%d" % (k, v) for k, v in sorted(counts.items())),
                        (msg.group(1) if msg else "")[:64]))
    tb = [l.strip() for l in lines if l.startswith(("AttributeError", "TypeError", "ValueError",
                                                   "KeyError", "RuntimeError"))]
    exits = [m for m in (EXIT.match(l) for l in lines) if m]
    last = exits[-1] if exits else None
    out.append("=== %s  %s   [%s]" % (tag, header, codec))
    out.append("  lines %d, goals accepted %d, feedback lines %d, results %d, run exit %s after %s s"
               % (len(lines), len(accepted), feedback, len(results),
                  last.group(1) if last else "?", last.group(2) if last else "?"))
    for i, (when, code, counts, msg) in enumerate(results[:3], 1):
        wall = ("%.1f s" % (float(when) - float(accepted[i - 1])) if i <= len(accepted) else "-")
        out.append("  result %d: t=%s wall=%s error_code=%s waypoints{%s} %s"
                   % (i, when, wall, code, counts, msg))
    if len(results) > 3:
        codes = {}
        for _, code, counts, _ in results:
            key = (code, counts)
            codes[key] = codes.get(key, 0) + 1
        out.append("  ... %d results in total; error_code/waypoint breakdown: %s"
                   % (len(results), ", ".join("%s x%d" % (k, v) for k, v in codes.items())))
    if tb:
        out.append("  tracebacks: %s" % " | ".join(tb[:2]))

text = "\n".join(out) + "\n"
io.open(os.path.join(HERE, "batch_results.txt"), "w", encoding="utf-8", newline="\n").write(text)
print(text)
