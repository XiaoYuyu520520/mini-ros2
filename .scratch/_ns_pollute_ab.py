import io, json, os, subprocess, sys, threading, time

ROOT = r"E:\DesktopData\app\code\CPU"
EXE = os.path.join(ROOT, "bridge", "windows", "build", "bridge_action_roundtrip.exe")
ROS2 = os.path.join(ROOT, "bridge", "windows", "ros2.bat")
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"

sys.stdout.reconfigure(encoding="utf-8")

def base_env():
    e = os.environ.copy()
    e["PATH"] = UCRT + os.pathsep + e.get("PATH", "")
    return e

def ros2(*args, timeout=60):
    d = subprocess.run(["cmd", "/c", ROS2, *args], cwd=ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=timeout)
    return d.stdout or ""

def run_many(env, n, results):
    for i in range(n):
        d = subprocess.run([EXE], cwd=ROOT, env=env, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
        results.append(d.returncode)

def poll(duration, tag):
    hits = []
    deadline = time.time() + duration
    while time.time() < deadline:
        for verb in (("node", "list"), ("action", "list"), ("service", "list")):
            out = ros2(*verb)
            for line in out.splitlines():
                s = line.strip()
                if not s:
                    continue
                low = s.lower()
                if "test/" in low or "test_" in low or "roundtrip" in low:
                    hits.append("%s %s: %s" % (verb[0], tag, s))
        time.sleep(0.3)
    return hits

print("daemon:", subprocess.run(["tasklist", "/fi", "imagename eq ros2_bridge_daemon.exe"],
                                capture_output=True, text=True).stdout.count("ros2_bridge_daemon"))

for tag, ns in (("A-unisolated", None), ("B-isolated", "probe_iso")):
    env = base_env()
    if ns:
        env["NAV2_SHM_NAMESPACE"] = ns
    rc = []
    th = threading.Thread(target=run_many, args=(env, 12, rc), daemon=True)
    th.start()
    hits = poll(35.0, tag)
    th.join(timeout=60)
    print("== %s: %d runs, rcs=%s" % (tag, len(rc), sorted(set(rc))))
    print("   graph hits: %s" % (sorted(set(hits))[:8] or "none"))