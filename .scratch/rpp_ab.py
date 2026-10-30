import os, subprocess, io, json

ROOT = r"E:\DesktopData\app\code\CPU"
BATDIR = os.path.join(ROOT, "bridge", "windows")

def ros2(args, timeout=180):
    p = subprocess.run("ros2.bat " + " ".join(args), shell=True, cwd=BATDIR,
                       capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("gbk", "replace").strip(), p.stderr.decode("gbk", "replace").strip()

def goal(report, timeout=300):
    p = subprocess.run([os.path.join(ROOT, ".scratch", "goal_run.bat"), "--goal", "auto",
                        "--report", report, "--status", "0"], shell=True,
                       capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("gbk", "replace")

lines = []
for name, sel in (("RPP (FollowPath)", "FollowPath"),):
    rc, out, err = ros2(["service", "call", "/sim/reset", "std_srvs/srv/Trigger"])
    lines.append("$ ros2 service call /sim/reset std_srvs/srv/Trigger -> %s" % (out.splitlines()[-1] if out else err))
    rc, out, err = ros2(["topic", "pub", "/controller_selector", "std_msgs/msg/String",
                         "data=" + sel, "--once"])
    lines.append("$ ros2 topic pub /controller_selector ... data=%s --once -> exit %d" % (sel, rc))
    rep = os.path.join(ROOT, ".scratch", "rpp_goal.json")
    if os.path.exists(rep):
        os.remove(rep)
    rc, out = goal(rep)
    lines.append("\n".join(out.strip().splitlines()[-8:]))
    lines.append("")

text = "\n".join(lines) + "\n"
io.open(os.path.join(ROOT, ".scratch", "rpp_ab.txt"), "w", encoding="utf-8", newline="\n").write(text)
print(text)

for f in ("graceful_goal.json", "rpp_goal.json"):
    p = os.path.join(ROOT, ".scratch", f)
    if os.path.exists(p):
        d = json.load(io.open(p, encoding="utf-8"))
        print("%-20s result=%-10s elapsed=%.1fs travelled=%.2fm remaining=%.2fm last_remaining=%.2fm recoveries=%s max_v=%.2f cmd_vel=%s plan=%s" % (
            f, d["result"], d["elapsed_s"], d["travelled_m"], d["remaining_m"],
            d["last_distance_remaining_m"], d["number_of_recoveries"],
            d["max_linear_velocity"], d["messages"]["cmd_vel"], d["messages"]["plan"]))