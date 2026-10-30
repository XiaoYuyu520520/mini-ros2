# -*- coding: utf-8 -*-
"""`ros2 topic pub` -> `ros2 topic echo` for every type the codec table gained.

The sweep in _codec_sweep.py proves the decode side on the topics nav2 really
publishes; this covers the types that only appear when a specific controller or
behavior is in use (DWB critics, waypoint follower feedback, collision monitor
state), plus Empty and the two rcl_interfaces topics nobody on this side
publishes.  Both directions here, on a topic this script owns.
"""
import io
import json
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
OUT = os.path.join(SC, "codec_pub_echo.out")
JSON_OUT = os.path.join(SC, "codec_pub_echo.json")

CASES = [
    ("std_msgs/msg/Empty", "std_msgs/msg/Empty", []),
    ("geometry_msgs/msg/PointStamped", "geometry_msgs/msg/PointStamped",
     ["header.frame_id=map", "point.x=1.5", "point.y=-2.25", "point.z=0.125"]),
    ("nav2_msgs/msg/SpeedLimit", "nav2_msgs/msg/SpeedLimit",
     ["header.frame_id=odom", "percentage=true", "speed_limit=0.35"]),
    ("nav2_msgs/msg/CostmapUpdate", "nav2_msgs/msg/CostmapUpdate",
     ["header.frame_id=odom", "x=1", "y=2", "size_x=3", "size_y=2",
      "data=0,253,254,255,1,99"]),
    ("map_msgs/msg/OccupancyGridUpdate", "map_msgs/msg/OccupancyGridUpdate",
     ["header.frame_id=map", "x=2", "y=3", "width=4", "height=3", "data=0,100,-1,0"]),
    ("nav2_msgs/msg/CostmapFilterInfo", "nav2_msgs/msg/CostmapFilterInfo",
     ["header.frame_id=map", "type=1", "filter_mask_topic=/keepout_mask",
      "base=0.0", "multiplier=-1.0"]),
    ("nav2_msgs/msg/VoxelGrid", "nav2_msgs/msg/VoxelGrid",
     ["header.frame_id=odom", "size_x=16", "size_y=16", "size_z=4",
      "resolutions.x=0.05", "resolutions.y=0.05", "resolutions.z=0.2",
      "origin.x=1.0", "origin.y=2.0"]),
    ("nav2_msgs/msg/TrackingFeedback", "nav2_msgs/msg/TrackingFeedback",
     ["header.frame_id=odom", "position_tracking_error=0.05",
      "heading_tracking_error=0.125", "current_path_index=7",
      "distance_to_goal=3.5", "speed=0.4", "remaining_path_length=4.25"]),
    ("nav2_msgs/msg/CollisionMonitorState", "nav2_msgs/msg/CollisionMonitorState",
     ["action_type=2", "polygon_name=StopPolygon"]),
    ("rcl_interfaces/msg/Log", "rcl_interfaces/msg/Log",
     ["level=20", "name=planner_server", "msg=hello rosout",
      "file=planner_server.cpp", "line=417", "function=computeControl"]),
    ("rcl_interfaces/msg/ParameterEvent", "rcl_interfaces/msg/ParameterEvent",
     ["node=/controller_server"]),
    ("visualization_msgs/msg/Marker", "visualization_msgs/msg/Marker",
     ["header.frame_id=odom", "ns=slowdown", "id=3", "type=4", "action=0",
      "pose.position.x=1.25", "pose.position.y=-0.5", "scale.x=0.1", "scale.y=0.2",
      "scale.z=1.0", "color.r=1.0", "color.g=0.0", "color.b=0.0", "color.a=0.5",
      "text=slow"]),
]

LOG = []


def say(text):
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), text)
    print(line, flush=True)
    LOG.append(line)


def run(args, t=90):
    done = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=t)
    return ((done.stdout or "") + (done.stderr or "")).strip()


def ros2(*args, t=90):
    return run([os.path.join(BAT, "ros2.bat"), *args], t=t)


rows = []
for index, (name, type_name, fields) in enumerate(CASES):
    topic = "/codec_check/%d" % index
    pub_log = io.open(os.path.join(SC, "codec_pub_%d.log" % index), "w", encoding="utf-8")
    publisher = subprocess.Popen(
        ["cmd", "/c", os.path.join(BAT, "ros2.bat"), "topic", "pub", topic, type_name,
         *fields, "--rate", "5", "--duration", "6"],
        cwd=ROOT, stdout=pub_log, stderr=subprocess.STDOUT)
    time.sleep(1.0)
    text = ros2("topic", "echo", topic, "--count", "1", "--timeout", "12", t=60)
    publisher.wait(timeout=30)
    pub_log.close()
    decoded = "---" in text and "(bytes of" not in text
    body = "\n".join(line for line in text.splitlines() if line.strip() != "---")
    rows.append({"topic": topic, "type": type_name, "decoded": decoded, "echo": body.strip()})
    say("%-34s %-42s %s" % (topic, type_name, "decoded" if decoded else "FAILED"))
    if body.strip():
        for line in body.strip().splitlines()[:4]:
            say("      " + line)

ok = [row for row in rows if row["decoded"]]
say("")
say("%d of %d types published and read back" % (len(ok), len(rows)))
io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(LOG) + "\n")
io.open(JSON_OUT, "w", encoding="utf-8", newline="\n").write(json.dumps(rows, indent=2) + "\n")
print("saved .scratch\\codec_pub_echo.out / .json")
