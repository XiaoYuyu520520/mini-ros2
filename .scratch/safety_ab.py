# -*- coding: utf-8 -*-
"""Item (1) A/B, with the chain intact in BOTH runs.

  A  nav2_stack.bat --safety                              -> the car drives
  B  nav2_stack.bat --safety --safety-params <widened>    -> the car cannot

Only the stop polygon differs. `cmd_vel_out_topic: cmd_vel` comes from
nav2_shim_safety_chain.yaml in both, so collision_monitor is the single writer of
the topic the robot listens to either way - which is what makes B mean "the
monitor stopped the robot" rather than "the chain was cut".
"""
import io, json, os, subprocess, sys, time
sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
WIDE = os.path.join(SC, "safety_stopall_chain.yaml")

LOG = []
def say(t):
    print(t, flush=True); LOG.append(t)

def run(args, t=240):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()

def ros2(*a, t=240):
    return run([os.path.join(BAT, "ros2.bat"), *a], t=t)

def trial(tag, extra):
    report = os.path.join(SC, "ab_%s.json" % tag)
    logp = os.path.join(SC, "ab_%s.log" % tag)
    if os.path.exists(report):
        os.remove(report)
    say("=" * 68)
    say("### %s : nav2_stack.bat --safety %s" % (tag, " ".join(os.path.basename(x) if x.endswith(".yaml") else x for x in extra)))
    say(ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger", t=120))

    fh = io.open(logp, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "nav2_stack.bat"), "--safety",
                             *extra, "--goal", "auto", "--report", report,
                             "--timeout", "90"],
                            cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT)
    deadline = time.time() + 240
    while time.time() < deadline and not os.path.exists(report):
        time.sleep(5)
    time.sleep(3)

    if os.path.exists(report):
        d = json.load(io.open(report, encoding="utf-8"))
        say("  result            : %s (%s) code=%s msg=%r" % (
            d.get("result"), d.get("reason"), d.get("error_code"), d.get("error_msg")))
        say("  goal / start      : %s  from %s" % (d.get("goal"), d.get("start_pose")))
        say("  final pose        : %s" % (d.get("final_pose"),))
        say("  travelled / left  : %.3f m / %.3f m" % (d.get("travelled_m", -1), d.get("remaining_m", -1)))
        say("  max linear vel    : %s" % d.get("max_linear_velocity"))
        say("  recoveries        : %s   elapsed %.1f s" % (d.get("number_of_recoveries"), d.get("elapsed_s", -1)))
        say("  messages          : %s" % d.get("messages"))
        say("  cmd_vel writers   : topic info /cmd_vel ->")
        say("    " + ros2("topic", "info", "/cmd_vel").replace("\n", "\n    "))
    else:
        say("  (no report written)")
    try:
        proc.kill()
    except Exception:
        pass
    fh.close()
    say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], t=180))
    say("  collision_monitor log tail:")
    cmlog = os.path.join(os.environ.get("TEMP", ""), "nav2_stack", "collision_monitor.log")
    if os.path.exists(cmlog):
        tail = [l for l in io.open(cmlog, encoding="utf-8", errors="replace").read().split("\n")
                if "stop" in l.lower() or "Polygon" in l or "cmd_vel" in l]
        for l in tail[-8:]:
            say("    " + l)
    return report

trial("A", [])
trial("B", ["--safety-params", WIDE])

say("=" * 68)
say("### summary")
for tag in ("A", "B"):
    p = os.path.join(SC, "ab_%s.json" % tag)
    if os.path.exists(p):
        d = json.load(io.open(p, encoding="utf-8"))
        say("  %s: %-9s travelled %6.3f m  max v %-5s  /cmd_vel %s" % (
            tag, d.get("result"), d.get("travelled_m", -1), d.get("max_linear_velocity"),
            d.get("messages", {}).get("cmd_vel")))

io.open(os.path.join(SC, "safety_ab.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\safety_ab.out")