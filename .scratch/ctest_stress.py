# -*- coding: utf-8 -*-
"""Run `ctest -j N` round after round and record what breaks.

`26/26` (now 27/27) serial says the shim is correct; it says nothing about
whether the shared memory, the graph file, the channel rings and the process
lifetime survive being exercised in parallel. They do not, always - there is a
history of `ctest -j` failures here (`ros2_shim_graph_rewrite`,
`ros2_shim_parameter_service`, `slam_toolbox_mapping`, and `shm_transport` once
hanging for 259 s until ctest killed it) that has never been reproduced on
demand, which is what this script is for.

Per round it records the start and end time, the duration, and the names of the
tests that failed; for a failing round it keeps the whole ctest output next to
the round number. At the end it prints, per test, how many rounds it failed and
the worst time it ever took - a test that is slow in one round and fails in the
next is the shape of a resource problem.

    python .scratch/ctest_stress.py --rounds 50 --jobs 8
    python .scratch/ctest_stress.py --rounds 20 --jobs 8 --fail-fast

Everything is pinned to the six P-cores, like the build and the serial run.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime
import json
import os
import re
import subprocess
import sys
import time

CORES = [0, 1, 10, 11, 12, 13]
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
BUILD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"
SCRATCH = r"E:\DesktopData\app\code\CPU\.scratch"


def pin() -> int:
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    mask = 0
    for cpu in CORES:
        mask |= 1 << cpu
    k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)
    return mask


# `      3/27 Test  #7: ros2_shim_graph_rewrite ...........   Passed    3.32 sec`
# and, when it fails, `***Failed` in the same column.
LINE = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(?P<name>\S+)\s+\.*\s*"
    r"(?P<status>Passed|\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception)\s+"
    r"(?P<secs>[0-9.]+)\s+sec", re.MULTILINE)


def run_round(index: int, jobs: int, timeout: float) -> dict:
    started = datetime.datetime.now()
    argv = [os.path.join(UCRT, "ctest.exe"), "--output-on-failure", "-j", str(jobs)]
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    began = time.monotonic()
    timed_out = False
    try:
        done = subprocess.run(
            argv, cwd=BUILD, env=env, capture_output=True, text=True,
            errors="replace", timeout=timeout)
        text = done.stdout + done.stderr
        rc = done.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        text = ((exc.stdout or b"").decode("utf-8", "replace") if isinstance(exc.stdout, bytes)
                else (exc.stdout or ""))
        rc = -1
    elapsed = time.monotonic() - began

    results = {m.group("name"): (m.group("status"), float(m.group("secs")))
               for m in LINE.finditer(text)}
    failed = sorted(n for n, (s, _) in results.items() if s != "Passed")
    summary = ""
    for line in text.splitlines():
        if "tests passed" in line or "tests failed" in line:
            summary = line.strip()
            break

    record = {
        "round": index,
        "started": started.isoformat(timespec="seconds"),
        "seconds": round(elapsed, 2),
        "returncode": rc,
        "timed_out": timed_out,
        "failed": failed,
        "summary": summary,
        "results": results,
    }
    if failed or timed_out or rc != 0:
        path = os.path.join(SCRATCH, "stress_fail_%03d.log" % index)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("$ ctest --output-on-failure -j %d\n" % jobs)
            handle.write(text)
        record["log"] = os.path.basename(path)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rounds", type=int, default=50)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="seconds before a round is killed (Default: 900)")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--tag", default="", help="suffix for the output files")
    args = parser.parse_args()

    mask = pin()
    print("ctest -j %d, %d rounds, pinned to cores %s (mask 0x%X)"
          % (args.jobs, args.rounds, ",".join(map(str, CORES)), mask), flush=True)

    records: list[dict] = []
    by_test: dict[str, dict] = {}
    for index in range(1, args.rounds + 1):
        record = run_round(index, args.jobs, args.timeout)
        records.append(record)
        for name, (status, secs) in record["results"].items():
            slot = by_test.setdefault(name, {"runs": 0, "failed": 0, "max_seconds": 0.0})
            slot["runs"] += 1
            slot["max_seconds"] = max(slot["max_seconds"], secs)
            if status != "Passed":
                slot["failed"] += 1
        flag = "FAIL " + ",".join(record["failed"]) if record["failed"] else "ok"
        print("[%3d/%d] %6.1fs  %s  %s"
              % (index, args.rounds, record["seconds"], flag, record["summary"]),
              flush=True)
        if args.fail_fast and (record["failed"] or record["timed_out"]):
            print("--fail-fast: stopping", flush=True)
            break

    json_path = os.path.join(SCRATCH, "ctest_stress%s.json" % args.tag)
    with open(json_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"jobs": args.jobs, "rounds": len(records), "records": records,
                   "by_test": by_test}, handle, indent=2, sort_keys=True)

    rounds_failed = sum(1 for r in records if r["failed"] or r["timed_out"])
    print("\n%d/%d rounds clean, %d rounds with a failed test"
          % (len(records) - rounds_failed, len(records), rounds_failed))
    print("%-42s %7s %7s %10s" % ("test", "runs", "failed", "worst s"))
    for name in sorted(by_test, key=lambda n: (-by_test[n]["failed"], -by_test[n]["max_seconds"])):
        slot = by_test[name]
        if slot["failed"] or slot["max_seconds"] > 60:
            print("%-42s %7d %7d %10.1f"
                  % (name, slot["runs"], slot["failed"], slot["max_seconds"]))
    print("wrote %s" % json_path)
    return 1 if rounds_failed else 0


if __name__ == "__main__":
    sys.exit(main())