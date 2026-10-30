import subprocess, os, io, time, re, json

root = os.getcwd()


def run(cmd, timeout=300):
    return subprocess.run(["cmd", "/c", cmd], cwd=root, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


tmp = os.environ.get("TEMP")
BLOG = os.path.join(tmp, "nav2_stack", "behavior_server.log")
CLOG = os.path.join(tmp, "nav2_stack", "controller_server.log")
pre = r'call bridge\windows\bridge_env.bat >nul 2>&1 && '


def size(p):
    return os.path.getsize(p) if os.path.exists(p) else 0


def delta(p, off):
    if not os.path.exists(p):
        return ""
    with io.open(p, encoding="utf-8", errors="replace") as f:
        f.seek(off)
        return f.read()


def tail(p, n=15):
    return "\n".join(io.open(p, encoding="utf-8", errors="replace").read().splitlines()[-n:])


run(r'bridge\windows\nav2_stack.bat stop')
time.sleep(3)
run(pre + r'bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger', timeout=60)
time.sleep(4)

h = io.open(".scratch\\stack_recovery.log", "w", encoding="utf-8", newline="")
proc = subprocess.Popen(
    ["cmd", "/c", r'call bridge\windows\bridge_env.bat >nul 2>&1 && bridge\windows\nav2_stack.bat --duration 900'],
    cwd=root, stdout=h, stderr=subprocess.STDOUT)
print("stack pid", proc.pid, flush=True)
txt = ""
deadline = time.time() + 150
while time.time() < deadline:
    time.sleep(2)
    txt = io.open(".scratch\\stack_recovery.log", encoding="utf-8", errors="replace").read()
    if "the stack is running" in txt:
        break
up = "the stack is running" in txt
print("up:", up, flush=True)
if not up:
    for n in ("controller_server", "bt_navigator", "behavior_server", "lifecycle_manager"):
        p = os.path.join(tmp, "nav2_stack", n + ".log")
        if os.path.exists(p):
            print("--- %s\n%s" % (n, tail(p)), flush=True)
    raise SystemExit(1)

time.sleep(6)
boff = size(BLOG)

echo_out = io.open(".scratch\\rec_cmdvel.txt", "w", encoding="utf-8", errors="replace")
echo = subprocess.Popen(["cmd", "/c", pre + r'bridge\windows\ros2.bat topic echo /cmd_vel'],
                        cwd=root, stdout=echo_out, stderr=subprocess.STDOUT)
time.sleep(4)

print("=== goal outside the map (16, 0), forcing the recovery branch", flush=True)
r = run(pre + r'nav2\ros2_shim\build\nav2_goal.exe --goal 16,0 --timeout 90 '
        r'--report .scratch\rec_goal.json', timeout=260)
io.open(".scratch/rec_goal.out", "w", encoding="utf-8", newline="\n").write(r.stdout)
for l in r.stdout.splitlines():
    if any(k in l for k in ("result", "reason", "robot", "plan ", "pipeline", "report")):
        print("   ", l.strip(), flush=True)

time.sleep(3)
echo.kill()
echo_out.close()

print("\n=== behavior_server.log", flush=True)
blog = delta(BLOG, boff)
io.open(".scratch/rec_behavior_server.log", "w", encoding="utf-8", newline="\n").write(blog)
for l in blog.splitlines():
    if any(k in l for k in ("Running ", "completed successfully", "Turning", "Backing", "wait", "backup", "spin")):
        print("   ", l.strip()[:160], flush=True)

print("\n=== /cmd_vel during recovery", flush=True)
raw = io.open(".scratch\\rec_cmdvel.txt", encoding="utf-8", errors="replace").read()
lin = [float(m) for m in re.findall(r"linear:\s*\{\s*x:\s*([-\d.eE+]+)", raw)]
ang = [float(m) for m in re.findall(r"angular:\s*\{\s*x:\s*[-\d.eE+]+,\s*y:\s*[-\d.eE+]+,\s*z:\s*([-\d.eE+]+)", raw)]
print("    messages with a linear.x: %d" % len(lin), flush=True)
if lin:
    print("    linear.x  min=%.3f  max=%.3f" % (min(lin), max(lin)), flush=True)
    print("    negative (backing up) samples: %d" % sum(1 for v in lin if v < -0.001), flush=True)
if ang:
    print("    angular.z min=%.3f  max=%.3f  (non-zero samples: %d)"
          % (min(ang), max(ang), sum(1 for v in ang if abs(v) > 0.001)), flush=True)

print("\n=== controller_server.log", flush=True)
clog = delta(CLOG, 0)
for l in clog.splitlines():
    if "Recovery" in l or "recovery" in l.lower() and "Selected" not in l:
        print("   ", l.strip()[:160], flush=True)