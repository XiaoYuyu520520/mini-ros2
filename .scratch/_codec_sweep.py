# -*- coding: utf-8 -*-
"""Live codec sweep: every topic in a real nav2 stack, decoded or only counted.

Run after the codec table grew from 19 to 38 topic types.  The stack is the
acceptance shape (daemon + nav2 with the safety chain), driven to a goal so the
topics that only publish while a plan is running are covered too.
"""
import io
import json
import os
import re
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
OUT = os.path.join(SC, "codec_sweep.out")
JSON_OUT = os.path.join(SC, "codec_sweep.json")
GOAL_REPORT = os.path.join(SC, "codec_sweep_goal.json")

LOG = []


def say(text):
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), text)
    print(line, flush=True)
    LOG.append(line)


def run(args, t=240):
    done = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=t)
    return ((done.stdout or "") + (done.stderr or "")).strip()


def ros2(*args, t=120):
    return run([os.path.join(BAT, "ros2.bat"), *args], t=t)


say("=== 0. clean slate ===")
run([r"taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240)
run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240)
time.sleep(2)

say("=== 1. daemon ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start"], t=240))
deadline = time.time() + 150
listing = ""
while time.time() < deadline:
    listing = ros2("topic", "list")
    if "/scan" in listing and "/odom" in listing:
        break
    time.sleep(3)
say("mirrored topics: %s" % " ".join(sorted(t for t in listing.split() if t.startswith("/"))))
say(ros2("service", "call", "/sim/status", "std_srvs/srv/Trigger"))

say("=== 2. nav2 with the safety chain, driving to a goal ===")
if os.path.exists(GOAL_REPORT):
    os.remove(GOAL_REPORT)
stack_log = io.open(os.path.join(SC, "codec_sweep_stack.log"), "w", encoding="utf-8")
cmdline = ('cmd /c %s --safety --goal "1.0,-1.0" --timeout 150 --report %s'
           % (os.path.join(BAT, "nav2_stack.bat"), GOAL_REPORT))
stack = subprocess.Popen(cmdline, cwd=ROOT, stdout=stack_log, stderr=subprocess.STDOUT)

say("waiting for the lifecycle manager to activate the stack")
deadline = time.time() + 240
while time.time() < deadline:
    names = ros2("node", "list", t=60)
    if "/bt_navigator" in names and "/controller_server" in names:
        break
    time.sleep(5)
say("nodes: %s" % " ".join(sorted(n for n in names.split() if n.startswith("/"))))

say("=== 3. interface list ===")
interfaces = [line.strip() for line in ros2("interface", "list").splitlines() if line.strip()]
say("the bridge can convert %d interfaces" % len(interfaces))
say(ros2("bridge", "status"))

say("=== 4. sweep: every topic, decoded or not ===")
rows = []
seen = set()
for attempt in range(2):
    listing = ros2("topic", "list", "-t")
    for line in listing.splitlines():
        match = re.match(r"^(\S+)\s+\[(.+)\]$", line.strip())
        if not match:
            continue
        topic, type_name = match.group(1), match.group(2)
        if topic in seen:
            continue
        seen.add(topic)
        supported = type_name in interfaces
        text = ros2("topic", "echo", topic, "--count", "1", "--timeout", "6", t=60) if supported else ""
        if not supported:
            verdict = "no codec"
            sample = ""
        elif "(bytes of" in text:
            verdict = "BYTE COUNT (unexpected)"
            sample = text.strip()
        elif "---" in text:
            verdict = "decoded"
            sample = "\n".join(line for line in text.splitlines() if line.strip() != "---")[:400]
        else:
            verdict = "silent (no writer in 6 s)"
            sample = ""
        rows.append({"topic": topic, "type": type_name, "supported": supported,
                     "verdict": verdict, "sample": sample})
        say("%-52s %-42s %s" % (topic, type_name, verdict))
    if attempt == 0 and not any(row["verdict"].startswith("decoded") for row in rows):
        say("nothing decoded yet - waiting for the goal to start")
        time.sleep(20)

types = sorted({row["type"] for row in rows})
unsupported_types = sorted({row["type"] for row in rows if not row["supported"]})
summary = {
    "topics": len(rows),
    "types_on_the_graph": len(types),
    "types_convertible": len([t for t in types if t in interfaces]),
    "types_without_codec": unsupported_types,
    "decoded": [row["topic"] for row in rows if row["verdict"] == "decoded"],
    "silent": [row["topic"] for row in rows if row["verdict"].startswith("silent")],
    "byte_count": [row["topic"] for row in rows if row["verdict"].startswith("BYTE")],
}
say("")
say("=== 5. summary ===")
say("topics on the graph      : %d" % summary["topics"])
say("distinct types           : %d" % summary["types_on_the_graph"])
say("types with a codec       : %d" % summary["types_convertible"])
say("types without a codec    : %d %s" % (len(unsupported_types), unsupported_types))
say("decoded topics           : %d" % len(summary["decoded"]))
say("silent topics            : %d %s" % (len(summary["silent"]), summary["silent"]))
say("byte-count fallbacks     : %d %s" % (len(summary["byte_count"]), summary["byte_count"]))

deadline = time.time() + 200
while time.time() < deadline:
    if os.path.exists(GOAL_REPORT):
        break
    time.sleep(5)
say("=== 6. goal report ===")
say(io.open(GOAL_REPORT, encoding="utf-8", errors="replace").read() if os.path.exists(GOAL_REPORT)
    else "no report")

say("=== 7. cleanup ===")
say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240))
stack_log.close()
summary["goal"] = os.path.exists(GOAL_REPORT)

io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(LOG) + "\n")
io.open(JSON_OUT, "w", encoding="utf-8", newline="\n").write(
    json.dumps({"summary": summary, "rows": rows}, indent=2, ensure_ascii=False) + "\n")
print("saved .scratch\\codec_sweep.out and .scratch\\codec_sweep.json")
