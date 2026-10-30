# -*- coding: utf-8 -*-
"""A/B the reader patience: 20 ctest rounds with 60 ms only, then 20 with the
shape-aware budget. The file is restored in a finally block.
"""
from __future__ import annotations

import ctypes
import io
import os
import re
import shutil
import subprocess
import sys
import time

CORES = [0, 1, 10, 11, 12, 13]
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
ROOT = r"E:\DesktopData\app\code\CPU"
BUILD = os.path.join(ROOT, "nav2", "ros2_shim", "build")
GRAPH = os.path.join(ROOT, "nav2", "ros2_shim", "include", "ros2_shim", "graph.hpp")
SNAP = os.path.join(ROOT, ".scratch", "graph_fixed_snapshot.hpp")
LINE = re.compile(r"(\d+) reads, (\d+) missing or half written")
NEW_BLOCK = "      const bool held = failure != ERROR_FILE_NOT_FOUND"


def pin() -> int:
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    mask = 0
    for cpu in CORES:
        mask |= 1 << cpu
    k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)
    return mask


def build() -> int:
    env = dict(os.environ)
    argv = [sys.executable, "-X", "utf8", os.path.join(ROOT, "nav2", "ros2_shim", "build.py"),
            "test_graph_rewrite"]
    done = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                          errors="replace", timeout=900)
    tail = [l for l in (done.stdout + done.stderr).splitlines() if l.strip()][-2:]
    print("    build: %s" % " | ".join(t.strip() for t in tail), flush=True)
    return done.returncode


def rounds(n: int, env_extra: dict) -> tuple:
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    env.update(env_extra)
    argv = [os.path.join(UCRT, "ctest.exe"), "--test-dir", BUILD,
            "-R", "^ros2_shim_graph_rewrite$", "--output-on-failure"]
    clean = 0
    total_missing = 0
    for i in range(1, n + 1):
        done = subprocess.run(argv, env=env, capture_output=True, text=True,
                              errors="replace", timeout=300)
        text = done.stdout + done.stderr
        m = LINE.search(text)
        missing = int(m.group(2)) if m else 0
        reads = int(m.group(1)) if m else 0
        total_missing += missing
        if done.returncode == 0:
            clean += 1
        else:
            print("      round %d FAILED: %s" % (i, m.group(0) if m else "?"), flush=True)
        print("    [%2d] rc=%d reads=%d missing=%d" % (i, done.returncode, reads, missing),
              flush=True)
    return clean, total_missing


def main() -> int:
    mask = pin()
    print("cores mask 0x%X" % mask, flush=True)
    fixed = io.open(GRAPH, encoding="utf-8", newline="").read()
    assert NEW_BLOCK in fixed, "the fix is not in graph.hpp"
    shutil.copyfile(GRAPH, SNAP)

    eol = "\r\n" if "\r\n" in fixed else "\n"
    old_block = eol.join([
        "      const bool held = failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND;",
        "      const std::chrono::milliseconds patience = held ?",
        "        std::chrono::milliseconds(1000) : std::chrono::milliseconds(60);",
    ])
    assert fixed.count(old_block) == 1
    reverted = fixed.replace(old_block, "      const std::chrono::milliseconds patience(60);")
    try:
        print("== BEFORE: every failed open gets 60 ms ==", flush=True)
        io.open(GRAPH, "w", encoding="utf-8", newline="").write(reverted)
        if build() != 0:
            return 2
        before = rounds(20, {"NAV2_SHM_NAMESPACE": "ab_before"})

        print("== AFTER: a held file gets 1000 ms ==", flush=True)
        io.open(GRAPH, "w", encoding="utf-8", newline="").write(fixed)
        if build() != 0:
            return 3
        after = rounds(20, {"NAV2_SHM_NAMESPACE": "ab_after"})
    finally:
        io.open(GRAPH, "w", encoding="utf-8", newline="").write(fixed)
        build()
        print("restored graph.hpp and rebuilt", flush=True)

    print("--- BEFORE (60 ms for everything): %d/20 clean, %d missed reads" % before)
    print("--- AFTER  (1000 ms when held)  : %d/20 clean, %d missed reads" % after)
    return 0


if __name__ == "__main__":
    sys.exit(main())