#!/usr/bin/env python3
"""Run `ctest -j N` round after round and record what breaks.

A serial `31/31` says the shim is correct; it says nothing about whether the
shared memory, the graph file, the channel rings and the process lifetime
survive being exercised in parallel. They do not, always -- there is a history of
`ctest -j` failures here (`ros2_shim_graph_rewrite`, `ros2_shim_parameter_service`,
`slam_toolbox_mapping`, and `shm_transport` once hanging for 259 s until ctest
killed it) that has never been reproduced on demand, which is what this script is
for.

Per round it records the start and end time, the duration and the names of the
tests that failed; a failing round keeps the whole ctest output next to its round
number. At the end it prints, per test, how many rounds it failed and the worst
time it ever took -- a test that is slow in one round and fails in the next is the
shape of a resource problem.

    python tools/ctest_stress.py --rounds 50 --jobs 8
    python tools/ctest_stress.py --rounds 20 --jobs 8 --tree bridge --fail-fast

Everything is pinned to the six P-cores, like the build and the serial runs (see
`tools/_cores.py`). Results land in `.scratch/` by default:
`ctest_stress[<tag>].json` plus `stress_fail_<round>.log` for any bad round.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _cores import big_cores, pin  # noqa: E402
from _toolchain import build_env, find_tools  # noqa: E402
from run_ctest import TREES, resolve  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]

# `      3/27 Test  #7: ros2_shim_graph_rewrite ...........   Passed    3.32 sec`
# and, when it fails, `***Failed` in the same column.
LINE = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(?P<name>\S+)\s+\.*\s*"
    r"(?P<status>Passed|\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception)\s+"
    r"(?P<secs>[0-9.]+)\s+sec", re.MULTILINE)


def run_round(index: int, tree: Path, ctest: Path, env: dict, jobs: int,
              timeout: float, out_dir: Path) -> dict:
    started = datetime.datetime.now()
    argv = [str(ctest), "--output-on-failure", "-j", str(jobs)]
    began = time.monotonic()
    timed_out = False
    try:
        done = subprocess.run(argv, cwd=str(tree), env=env, capture_output=True,
                              text=True, errors="replace", timeout=timeout)
        text, rc = done.stdout + done.stderr, done.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        text = ((exc.stdout or b"").decode("utf-8", "replace")
                if isinstance(exc.stdout, bytes) else (exc.stdout or ""))
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

    record = {"round": index, "started": started.isoformat(timespec="seconds"),
              "seconds": round(elapsed, 2), "returncode": rc, "timed_out": timed_out,
              "failed": failed, "summary": summary, "results": results}
    if failed or timed_out or rc != 0:
        path = out_dir / ("stress_fail_%03d.log" % index)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("$ ctest --output-on-failure -j %d\n" % jobs)
            handle.write(text)
        record["log"] = path.name
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rounds", type=int, default=50)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--tree", default="shim",
                        help="short name (%s) or a build directory" % ", ".join(TREES))
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="seconds before a round is killed (Default: 900)")
    parser.add_argument("--out", default=str(REPO_ROOT / ".scratch"),
                        help="where the json and the failing rounds are written")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--tag", default="", help="suffix for the output files")
    args = parser.parse_args()

    tree = resolve(args.tree)
    if not (tree / "CTestTestfile.cmake").is_file():
        raise SystemExit("not a ctest tree (no CTestTestfile.cmake): %s" % tree)
    tools = find_tools(need_ninja=False)
    if tools.ctest is None:
        raise SystemExit("no ctest next to %s -- set CMAKE or install ctest"
                         % tools.cmake)
    env = build_env(tools)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    note = pin()
    print("ctest -j %d, %d rounds, %s -- %s"
          % (args.jobs, args.rounds, tree, note), flush=True)

    records: list[dict] = []
    by_test: dict[str, dict] = {}
    for index in range(1, args.rounds + 1):
        record = run_round(index, tree, tools.ctest, env, args.jobs, args.timeout,
                           out_dir)
        records.append(record)
        for name, (status, secs) in record["results"].items():
            slot = by_test.setdefault(name, {"runs": 0, "failed": 0, "max_seconds": 0.0})
            slot["runs"] += 1
            slot["max_seconds"] = max(slot["max_seconds"], secs)
            if status != "Passed":
                slot["failed"] += 1
        flag = "FAIL " + ",".join(record["failed"]) if record["failed"] else "ok"
        print("[%3d/%d] %6.1fs  %s  %s"
              % (index, args.rounds, record["seconds"], flag, record["summary"]), flush=True)
        if args.fail_fast and (record["failed"] or record["timed_out"]):
            print("--fail-fast: stopping", flush=True)
            break

    json_path = out_dir / ("ctest_stress%s.json" % args.tag)
    with open(json_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"jobs": args.jobs, "tree": str(tree), "rounds": len(records),
                   "records": records, "by_test": by_test}, handle, indent=2, sort_keys=True)

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