# -*- coding: utf-8 -*-
"""Direct runs, this time honouring ENVIRONMENT_MODIFICATION (per-test namespace)."""
import io
import json
import os
import re
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")
ROOT = r"E:\DesktopData\app\code\CPU"
SC = os.path.join(ROOT, ".scratch")
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
env = dict(os.environ)
env["PATH"] = UCRT + os.pathsep + env["PATH"]
BUILDS = [r"nav2\ros2_shim\build", r"bridge\windows\build", r"gRPC\ros2_gRPC_sdk\build"]

tests = {}
for rel in BUILDS:
    bd = os.path.join(ROOT, rel)
    out = subprocess.run([os.path.join(UCRT, "ctest.exe"), "--show-only=json-v1"], cwd=bd, env=env,
                         capture_output=True, text=True, encoding="utf-8", errors="replace",
                         timeout=300).stdout
    for t in json.loads(out)["tests"]:
        extra = {}
        cwd = bd
        for e in t.get("properties", []):
            if e["name"] == "ENVIRONMENT":
                for item in e["value"]:
                    k, _, v = item.partition("=")
                    extra[k] = v
            elif e["name"] == "ENVIRONMENT_MODIFICATION":
                for item in e["value"]:
                    k, _, v = item.partition("=")
                    if v.startswith("set:"):
                        extra[k] = v[4:]
                    elif v.startswith("unset:"):
                        extra[k] = ""
                    else:
                        print("!! unhandled ENVIRONMENT_MODIFICATION %s=%s" % (k, v))
            elif e["name"] == "WORKING_DIRECTORY":
                cwd = e["value"]
        tests[t["name"]] = (cwd, t["command"], extra)

rows = []
for name in sorted(tests):
    cwd, cmd, extra = tests[name]
    run_env = dict(env)
    run_env.update(extra)
    log = os.path.join(SC, "ns_%s.log" % re.sub(r"[^\w]", "_", name))
    t0 = time.time()
    with io.open(log, "w", encoding="utf-8") as handle:
        p = subprocess.run(cmd, cwd=cwd, env=run_env, stdout=handle,
                           stderr=subprocess.STDOUT, timeout=600)
    dt = time.time() - t0
    text = io.open(log, encoding="utf-8", errors="replace").read()
    ok = len(re.findall(r"(?m)^\s*(?:ok|PASS|pass)\b", text)) + len(re.findall(r"\[PASS\]", text))
    bad = len(re.findall(r"(?m)^\s*(?:FAIL|fail)\b", text)) + len(re.findall(r"\[FAIL\]", text))
    rows.append({"name": name, "rc": p.returncode, "sec": round(dt, 2), "pass": ok,
                 "fail": bad, "ns": extra.get("NAV2_SHM_NAMESPACE", "-")})

print("%-38s %4s %7s %5s %5s  %s" % ("ctest name", "rc", "sec", "pass", "fail", "namespace"))
for r in rows:
    print("%-38s %4d %7.2f %5d %5d  %s" % (r["name"], r["rc"], r["sec"], r["pass"], r["fail"], r["ns"]))
bad = [r["name"] for r in rows if r["rc"] != 0 or r["fail"]]
print()
print("nonzero exit or reported failure: %s" % (bad or "none"))
print("total printed assertion lines: %d pass / %d fail" % (sum(r["pass"] for r in rows),
                                                            sum(r["fail"] for r in rows)))
json.dump(rows, io.open(os.path.join(SC, "ns_assertions.json"), "w", encoding="utf-8"), indent=1)
print("saved .scratch\\ns_assertions.json")