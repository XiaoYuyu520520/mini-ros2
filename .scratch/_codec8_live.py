
import io, os, subprocess, sys, time

ROOT = r"E:\DesktopData\app\code\CPU"
BATDIR = os.path.join(ROOT, "bridge", "windows")
BUILD = os.path.join(ROOT, "nav2", "ros2_shim", "build")
SCRATCH = os.path.join(ROOT, ".scratch")
OUT = os.path.join(SCRATCH, "codec8_live.out")
LOG = []

def say(line):
    print(line, flush=True)
    LOG.append(line)

def ros2(*args, timeout=120):
    p = subprocess.run(["cmd", "/c", os.path.join(BATDIR, "ros2.bat")] + list(args),
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).strip()

def service_names():
    rc, out = ros2("service", "list")
    return [l.strip() for l in out.split("\n") if l.strip().startswith("/")]

def wait_for(pred, seconds, label):
    start = time.time()
    while time.time() - start < seconds:
        names = service_names()
        hit = pred(names)
        if hit:
            say("  [wait] %s after %.1fs" % (label, time.time() - start))
            return hit
        time.sleep(2.0)
    say("  [wait] %s NOT seen after %.1fs" % (label, seconds))
    return None

def call(name, type_name, *assignments):
    rc, out = ros2("service", "call", name, type_name, *assignments)
    say("$ ros2 service call %s %s %s" % (name, type_name, " ".join(assignments)))
    for line in out.split("\n"):
        say("  " + line)
    say("  [exit %d]" % rc)
    return rc, out

def bat(name, *args):
    return subprocess.Popen(["cmd", "/c", os.path.join(BATDIR, name)] + list(args),
                            cwd=BATDIR, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)

def stop_bat(name):
    p = subprocess.run(["cmd", "/c", os.path.join(BATDIR, name), "stop"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=180)
    say("  [stop] %s rc=%d %s" % (name, p.returncode, p.stdout.strip()[:120]))

# ---------------------------------------------------------------------------
say("=== A. collision_monitor: add / remove exclusion zone ===")
safety = bat("nav2_safety.bat", "--duration", "120")
try:
    hit = wait_for(lambda n: "/add_exclusion_zone" in n, 90, "/add_exclusion_zone")
    if hit:
        call("/add_exclusion_zone", "nav2_msgs/srv/AddExclusionZone",
             "zone.zone_name=keepout_a", "zone.type=circle", "zone.frame_id=map",
             "zone.radius=0.45", "zone.enabled=true")
        call("/remove_exclusion_zone", "nav2_msgs/srv/RemoveExclusionZone",
             "zone_name=keepout_a")
finally:
    stop_bat("nav2_safety.bat")
    try:
        safety.wait(timeout=60)
    except Exception:
        safety.kill()

# ---------------------------------------------------------------------------
say("")
say("=== B. vector_object_server: add / get / remove shapes ===")
vo = subprocess.Popen(["cmd", "/c", os.path.join(SCRATCH, "vo_server.bat")], cwd=SCRATCH,
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
mgr = subprocess.Popen(["cmd", "/c", os.path.join(SCRATCH, "vo_manager.bat")], cwd=SCRATCH,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
try:
    hit = wait_for(lambda n: "/vector_object_server/get_shapes" in n, 60,
                   "/vector_object_server/get_shapes")
    if hit:
        call("/vector_object_server/get_shapes", "nav2_msgs/srv/GetShapes")
        call("/vector_object_server/add_shapes", "nav2_msgs/srv/AddShapes")
        call("/vector_object_server/remove_shapes", "nav2_msgs/srv/RemoveShapes",
             "all_objects=true")
        call("/vector_object_server/get_shapes", "nav2_msgs/srv/GetShapes")
finally:
    for proc in (mgr, vo):
        try:
            proc.wait(timeout=40)
        except Exception:
            proc.kill()

# ---------------------------------------------------------------------------
say("")
say("=== C. route_server + docking_server: graph and dock database ===")
dr = bat("nav2_docking_route.bat", "--duration", "120")
try:
    hit = wait_for(lambda n: "/route_server/set_route_graph" in n, 90,
                   "/route_server/set_route_graph")
    if hit:
        names = service_names()
        edges = [n for n in names if "adjust_edges" in n or "dynamic_edges" in n]
        say("  [services] edge services: %s" % (edges or "(none)"))
        say("  [services] dock services: %s" %
            ([n for n in names if "reload" in n] or "(none)"))
        call("/route_server/set_route_graph", "nav2_msgs/srv/SetRouteGraph",
             "graph_filepath=C:/maps/nope.geojson")
        if edges:
            call(edges[0], "nav2_msgs/srv/DynamicEdges",
                 "closed_edges=1,7,300", "opened_edges=9")
        reload_names = [n for n in names if "reload" in n]
        if reload_names:
            call(reload_names[0], "nav2_msgs/srv/ReloadDockDatabase",
                 "filepath=C:/maps/nope.yaml")
finally:
    stop_bat("nav2_docking_route.bat")
    try:
        dr.wait(timeout=60)
    except Exception:
        dr.kill()

io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(LOG) + "\n")
say("")
say("wrote " + OUT)
