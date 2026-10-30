import os, subprocess, time
ROOT = r"E:\DesktopData\app\code\CPU"
r = subprocess.run(["cmd", "/c", r"bridge\windows\ros2_bridge.bat", "stop"],
                   cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=120)
print("stop rc", r.returncode)
print((r.stdout or "") + (r.stderr or ""))[:1500]
time.sleep(2)
out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
for line in out.splitlines():
    if "bridge" in line.lower() or "ros2" in line.lower():
        print("  ", line.strip())