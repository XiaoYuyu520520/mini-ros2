# -*- coding: utf-8 -*-
"""Live proof that the *default* uplink list carries nav2's results to the robot.

Before this run the daemon needed ``--uplink /plan`` spelled out; the claim under
test is that a plain ``ros2_bridge.bat start`` is enough.  The robot side watches
``/plan`` (``run_sim.py --subscribe /plan:nav_msgs/msg/Path``) and logs every
message, so the evidence is on the far end of the wire, not in the daemon's own
counter.
"""
import io, json, os, re, subprocess, sys, time

sys.stdout.reconfigure(encoding="utf-8")
ROOT = r"E:\DesktopData\app\code\CPU"
SC = os.path.join(ROOT, ".scratch")
REPORT = os.path.join(SC, "_uplink_plan_report.json")
LOG = os.path.join(SC, "_uplink_plan_demo.log")
SAMPLES = os.path.join(SC, "_uplink_plan_samples.txt")

def run(args, t=300, cwd=ROOT):
    return subprocess.run(["cmd", "/c", *args], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=t)

def status_uplinks():
    d = run([os.path.join(ROOT, "bridge", "windows", "ros2_bridge.bat"), "status"])
    out = (d.stdout or "") + (d.stderr or "")
    keep = [l for l in out.split("\n") if l.strip().startswith("^") or "uplink=" in l]
    return "\n".join(keep)

if os.path.exists(REPORT):
    os.remove(REPORT)

print("=== before the run: uplink table with no nav2 running ===")
before = status_uplinks()
print(before, flush=True)

log = io.open(LOG, "w", encoding="utf-8")
proc = subprocess.Popen([sys.executable, os.path.join(ROOT, "bridge", "scripts", "run_nav2_demo.py"),
                         "--goal", "auto", "--timeout", "150", "--report", REPORT],
                        cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)

samples = []
deadline = time.time() + 420
last = ""
while time.time() < deadline:
    if proc.poll() is not None and os.path.exists(REPORT):
        break
    try:
        snap = status_uplinks()
    except Exception as exc:
        snap = "status failed: %s" % exc
    if snap != last:
        samples.append("[%s]\n%s" % (time.strftime("%H:%M:%S"), snap))
        last = snap
    time.sleep(10)
if proc.poll() is None:
    time.sleep(5)
print("=== during/after: every distinct uplink table ===")
print("\n\n".join(samples) if samples else "(no change observed)")
io.open(SAMPLES, "w", encoding="utf-8").write("\n\n".join(samples))

print("=== final uplink table ===")
final = status_uplinks()
print(final)
print("=== demo log tail ===")
print(io.open(LOG, encoding="utf-8", errors="replace").read()[-2500:])
print("=== report ===")
print(io.open(REPORT, encoding="utf-8", errors="replace").read() if os.path.exists(REPORT) else "missing")
print("exit", proc.returncode)
