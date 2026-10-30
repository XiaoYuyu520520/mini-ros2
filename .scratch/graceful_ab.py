import os, subprocess, io

ROOT = r"E:\DesktopData\app\code\CPU"
BATDIR = os.path.join(ROOT, "bridge", "windows")
OUT = os.path.join(ROOT, ".scratch", "graceful_ab.txt")

def ros2(args, timeout=180):
    p = subprocess.run("ros2.bat " + " ".join(args), shell=True, cwd=BATDIR,
                       capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("gbk", "replace").strip(), p.stderr.decode("gbk", "replace").strip()

def goal(args, timeout=300):
    p = subprocess.run([os.path.join(ROOT, ".scratch", "goal_run.bat")] + args, shell=True,
                       capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("gbk", "replace"), p.stderr.decode("gbk", "replace")

lines = []

def step(title, rc, out, err):
    lines.append("$ " + title)
    if out.strip():
        lines.append(out.rstrip())
    if err.strip():
        lines.append("[stderr] " + err.rstrip())
    lines.append("[exit %d]" % rc)
    lines.append("")

rc, out, err = ros2(["service", "call", "/sim/reset", "std_srvs/srv/Trigger"])
step("ros2 service call /sim/reset std_srvs/srv/Trigger", rc, out, err)

rc, out, err = ros2(["topic", "pub", "/controller_selector", "std_msgs/msg/String",
                     "data=GracefulFollowPath", "--once"])
step("ros2 topic pub /controller_selector std_msgs/msg/String data=GracefulFollowPath --once",
     rc, out, err)

rc, out, err = goal(["--goal", "auto", "--report",
                     os.path.join(ROOT, ".scratch", "graceful_goal.json"), "--status", "1.0"])
step("nav2_goal.exe --goal auto --report .scratch\\graceful_goal.json", rc, out, err)

text = "\n".join(lines) + "\n"
io.open(OUT, "w", encoding="utf-8", newline="\n").write(text)
print(text)