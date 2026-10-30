# -*- coding: utf-8 -*-
# Phase 2 of the persistence loop, on its own: the mapping run and the
# serialize_map call already happened (.scratch/slam_persist.posegraph /
# .data exist). This script only asks one question - does a fresh
# localization_slam_toolbox process get that map back?
#
# The previous run started the node with `-p map_file_name:=...` and nothing
# else, and the log carried exactly one line about it:
#
#   [ERROR] [slam_toolbox] LocalizationSlamToolbox: Map starting pose not
#   specified. Set either map_start_pose or map_start_at_dock.
#
# slam_toolbox_common.cpp shouldStartWithPoseGraph() returns false in that
# case, before it ever opens the file, so the node activates on an empty map.
# So the three arms below are: with map_start_pose, with map_start_at_dock,
# and the bare one as the control.
import io, os, subprocess, sys, time

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
BASE = "E:/DesktopData/app/code/CPU/.scratch/slam_persist"
PGRPH = os.path.join(SC, "slam_persist.posegraph")

sys.path.insert(0, os.path.join(ROOT, "bridge", "scripts"))
sys.stdout.reconfigure(encoding="utf-8")
import acceptance_check as ac

LOG = []

def say(t):
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), t)
    print(line, flush=True)
    LOG.append(line)

def run(args, t=300):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()

def ros2(*a, t=120):
    return run([os.path.join(BAT, "ros2.bat"), *a], t)

def slam_log(tag):
    return os.path.join(SC, "slam_reload_%s.log" % tag)

def wait_map_publisher(seconds=120):
    deadline = time.time() + seconds
    while time.time() < deadline:
        info = ros2("topic", "info", "/map", t=60)
        if "Publisher count: 1" in info:
            return True
        time.sleep(4)
    return False

# ------------------------------------------------------------------ setup
say("=== 0. clean slate ===")
run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240)
time.sleep(2)
say("pose graph on disk: exists=%s bytes=%s"
    % (os.path.exists(PGRPH), os.path.getsize(PGRPH) if os.path.exists(PGRPH) else "-"))
if not os.path.exists(PGRPH):
    say("ABORT: no pose graph to load")
    io.open(os.path.join(SC, "slam_reload_ab2.out"), "w", encoding="utf-8").write("\n".join(LOG))
    raise SystemExit(1)

say("=== 1. daemon without /map (SLAM owns it) ===")
run([os.path.join(BAT, "ros2_bridge.bat"), "start", "--mirror", "/clock",
     "--mirror", "/odom", "--mirror", "/scan", "--mirror", "/tf",
     "--mirror", "/tf_static"], 240)
say("topics: " + " ".join(sorted(x for x in ros2("topic", "list").split() if x.startswith("/"))))
say("reset: " + ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger", t=60).splitlines()[-1])
time.sleep(3)

ARMS = [
    ("pose",    ["-p", "map_start_pose:=[0.0,0.0,0.0]"]),
    ("dock",    ["-p", "map_start_at_dock:=true"]),
    ("bare",    []),
    # Three integers where the declared type is a double array: that cannot be
    # read as what the node declares, so the node keeps the default - and now
    # says which parameter it dropped, instead of leaving only slam_toolbox's
    # own "Map starting pose not specified" behind.
    ("poseint", ["-p", "map_start_pose:=[0,0,0]"]),
]

for tag, extra in ARMS:
    say("=== arm %s: %s ===" % (tag, " ".join(extra) if extra else "(no pose parameter)"))
    f = io.open(slam_log(tag), "w", encoding="utf-8")
    args = ["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
            "--mode", "localization",
            "-p", "base_frame:=base_link",
            "-p", "map_file_name:=" + BASE,
            "-p", "max_laser_range:=12.0",
            "-p", "map_update_interval:=2.0",
            *extra, "--duration", "120"]
    p = subprocess.Popen(args, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
    ok = wait_map_publisher(120)
    say("  /map publisher: %s" % ok)
    if ok:
        time.sleep(10)
        try:
            dump = ac.map_dump(topic="/map", settle=6.0, ascii_width=40,
                               out=os.path.join(SC, "slam_reload_%s.pgm" % tag))
            say("  MAP %dx%d res=%.3f origin=(%.2f,%.2f) occupied=%d free=%d unknown=%d"
                % (dump["width"], dump["height"], dump["resolution"],
                   dump["origin"][0], dump["origin"][1],
                   dump["occupied"], dump["free"], dump["unknown"]))
        except Exception as exc:
            say("  map_dump failed: %s" % exc)
    run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
    if p.poll() is None:
        p.kill()
    f.close()
    time.sleep(2)
    txt = io.open(slam_log(tag), encoding="utf-8", errors="replace").read()
    load = [l for l in txt.splitlines()
            if any(k in l for k in ("serialization", "Deserialize", "Failed to",
                                    "starting pose", "Loading", "deserialize",
                                    "was overridden"))]
    say("  log lines about loading (%d):" % len(load))
    for l in load[:12]:
        say("    " + l.strip()[:200])
    say("  log tail: " + txt.strip().splitlines()[-1][:200] if txt.strip() else "  (empty log)")

say("=== cleanup ===")
run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240)
run([os.path.join(BAT, "ros2_bridge.bat"), "start"], 240)
say("restored topics: " + " ".join(sorted(x for x in ros2("topic", "list").split() if x.startswith("/"))))

io.open(os.path.join(SC, "slam_reload_ab2.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\slam_reload_ab2.out")
