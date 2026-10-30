import os, subprocess, sys
base = r"E:\DesktopData\app\code\CPU"
print("== nav2 ==")
for n in sorted(os.listdir(os.path.join(base,"nav2"))):
    print("  ", n)
print("== .scratch (e2e/map_saver/run_) ==")
sc = os.path.join(base, ".scratch")
for n in sorted(os.listdir(sc)):
    if any(k in n.lower() for k in ("e2e","map_saver","run_","serial")):
        print("  ", n, os.path.getsize(os.path.join(sc,n)))
print("== procs ==")
out = subprocess.run(["tasklist","/fo","csv","/nh"], capture_output=True, text=True).stdout
for line in out.splitlines():
    low = line.lower()
    if any(k in low for k in ("ros2","slam","bridge","ros2_bridge")):
        print("  ", line.strip())