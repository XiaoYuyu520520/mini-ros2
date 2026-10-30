import os, subprocess, sys, threading, time
ROOT = r"E:\DesktopData\app\code\CPU"
CTEST = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin\ctest.exe"
ROS2 = os.path.join(ROOT, "bridge", "windows", "ros2.bat")
BUILD = os.path.join(ROOT, "bridge", "windows", "build")
sys.stdout.reconfigure(encoding="utf-8")

def ros2(*args, timeout=60):
    d = subprocess.run(["cmd", "/c", ROS2, *args], cwd=ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=timeout)
    return d.stdout or ""

res = {}
def run_ctest():
    d = subprocess.run([CTEST, "--test-dir", BUILD, "--repeat", "until-fail:8",
                        "--output-on-failure"], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=600)
    res["out"] = (d.stdout or "") + (d.stderr or "")
    res["rc"] = d.returncode

hits = []
def poll(duration):
    deadline = time.time() + duration
    while time.time() < deadline:
        for verb in (("node", "list"), ("action", "list"), ("service", "list")):
            for line in ros2(*verb).splitlines():
                s = line.strip().lower()
                if "test/" in s or "roundtrip" in s:
                    hits.append("%s: %s" % (verb[0], line.strip()))
        time.sleep(0.3)

print("daemon running:",
      subprocess.run(["tasklist", "/fi", "imagename eq ros2_bridge_daemon.exe"],
                     capture_output=True, text=True).stdout.count("ros2_bridge_daemon"))
th = threading.Thread(target=run_ctest, daemon=True)
th.start()
poll(45.0)
th.join(timeout=200)
out = res.get("out", "")
print("--- ctest tail ---")
print("\n".join(out.strip().splitlines()[-8:]))
print("rc =", res.get("rc"))
print("graph pollution hits:", sorted(set(hits)) or "NONE")