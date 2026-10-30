import pathlib, subprocess
root = pathlib.Path(r"E:\DesktopData\app\code\CPU")
bad = []
for f in root.rglob("*.bat"):
    if "build" in f.parts or "third_party" in f.parts:
        continue
    b = f.read_bytes()
    lf_only = b.replace(b"\r\n", b"").count(b"\n")
    if lf_only:
        bad.append((str(f.relative_to(root)), lf_only))
print("bat files with bare LF:", bad)
bat = subprocess.run(["cmd", "/c", r"call bridge\windows\nav2_stack.bat stop"],
                     cwd=str(root), capture_output=True, text=True).stdout
print(bat.strip().splitlines()[-1])
out = subprocess.run(["tasklist", "/fo", "csv"], capture_output=True, text=True).stdout
left = [l for l in out.splitlines()
        if any(k in l.lower() for k in ("controller_server", "planner_server", "bt_navig",
                                        "behavior_server", "smoother", "waypoint", "lifecycle",
                                        "nav2_goal", "amcl", "ros2_bridge"))]
print("leftovers:")
for l in left:
    print("  ", l)