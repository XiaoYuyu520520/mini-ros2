import os, re, subprocess, sys, tempfile, time

EXE = r"nav2\ros2_shim\build\slam_toolbox_port\test_slam_mapping.exe"
MSYS = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"

def env_for(tag, extra=None):
    e = os.environ.copy()
    e["PATH"] = MSYS + os.pathsep + e.get("PATH", "")
    e.update({
        "NAV2_SHM_SLOT_COUNT": "8",
        "NAV2_SHM_SLOT_SIZE": str(2 << 20),
        "NAV2_SHM_LARGE_SLOT_SIZE": str(64 << 20),
        "NAV2_SHM_GRAPH_DIR": os.path.join(tempfile.gettempdir(), "slam_race_" + tag),
        "ROS2_SHM_GRAPH_DIR": os.path.join(tempfile.gettempdir(), "slam_race_" + tag),
        "NAV2_SHM_LOG_LEVEL": "warn",
    })
    if extra:
        e.update(extra)
    return e

def run(tag, settle):
    extra = {"SLAM_MAPPING_SETTLE_MS": str(settle)} if settle is not None else {}
    t0 = time.time()
    r = subprocess.run([EXE], env=env_for(tag, extra), capture_output=True, text=True, errors="replace")
    out = r.stdout + r.stderr
    drift = re.search(r"worst drift ([\d.]+) m / ([\d.]+) deg", out)
    grid = re.search(r"grid (\d+)x(\d+) @ ([\d.]+)m, (\d+) known, (\d+) occupied", out)
    span = re.search(r"wall bounding box: ([\d.]+) m x ([\d.]+) m", out)
    fails = len(re.findall(r"^\s+FAIL:", out, re.M))
    print(f"settle={settle} rc={r.returncode} drift={drift.groups() if drift else None} "
          f"grid={grid.groups() if grid else None} span={span.groups() if span else None} fails={fails} "
          f"{time.time()-t0:.1f}s", flush=True)
    return drift.group(1) if drift else "?"

for settle in [0, 0, 0, 200, 200, 200]:
    run("s%d_%d" % (settle, int(time.time()*1000) % 100000), settle)
