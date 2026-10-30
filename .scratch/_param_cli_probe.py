# `ros2 param set` takes a list; does it still, and does a name that merely
# contains a `[n]` still stay a name? Both go through
# `ros2_shim::parseParameterText`, which this round tightened: the list now has
# to be the *whole* value instead of a `[` anywhere with a `]` after it.
import io, os, subprocess, sys, time

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
BASE = "E:/DesktopData/app/code/CPU/.scratch/slam_persist"

sys.stdout.reconfigure(encoding="utf-8")
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

say("=== daemon, default mirrors ===")
run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start"], 240) or "(already up)")
say("topics: " + " ".join(sorted(x for x in ros2("topic", "list").split() if x.startswith("/"))))

say("=== a localisation node to set parameters on ===")
log = io.open(os.path.join(SC, "slam_param_cli.log"), "w", encoding="utf-8")
p = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
                      "--mode", "localization",
                      "-p", "base_frame:=base_link",
                      "-p", "map_file_name:=" + BASE,
                      "-p", "map_start_at_dock:=true",
                      "--duration", "120"],
                     cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
for _ in range(30):
    if "/slam_toolbox" in ros2("node", "list", t=60):
        break
    time.sleep(3)
time.sleep(3)

say("=== a list through `ros2 param set` ===")
say("set  -> " + ros2("param", "set", "/slam_toolbox", "map_start_pose", "[1.5,-2.5,0.25]", t=60))
say("get  -> " + ros2("param", "get", "/slam_toolbox", "map_start_pose", t=60))

say("=== a name that merely contains brackets ===")
say("set  -> " + ros2("param", "set", "/slam_toolbox", "map_file_name", "C:/maps/a[2]/walls.yaml", t=60))
say("get  -> " + ros2("param", "get", "/slam_toolbox", "map_file_name", t=60))

say("=== an empty list is still refused, with the reason ===")
say("set  -> " + ros2("param", "set", "/slam_toolbox", "map_start_pose", "[]", t=60))

say("=== cleanup ===")
run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
if p.poll() is None:
    p.kill()
log.close()
say("topics: " + " ".join(sorted(x for x in ros2("topic", "list").split() if x.startswith("/"))))

io.open(os.path.join(SC, "param_cli_probe.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\param_cli_probe.out")
