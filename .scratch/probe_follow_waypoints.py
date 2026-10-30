# -*- coding: utf-8 -*-
"""Does FollowWaypoints fix the lap loop that NavigateThroughPoses loses?

The probe before this one showed a resent NavigateThroughPoses goal comes back
in 0.11 s with the car standing still, because that navigator judges success by
the controller's goal checker against the *end* of the path. FollowWaypoints
processes the poses one at a time, so its success is per waypoint.

  G1  the same 9 poses, via /follow_waypoints
  G2  the very same 9 poses again

A G2 that really drives (~70 s, hundreds of feedbacks) is the answer: laps in
the racecar mission scripts belong on FollowWaypoints, not on a resend.
"""
from __future__ import annotations

import csv
import os
import sys
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import FollowWaypoints
from geometry_msgs.msg import PoseStamped

HERE = os.path.dirname(os.path.abspath(__file__))
STEM = os.path.dirname(HERE)
CSV = os.path.join(STEM, "code", "Desktop", "racecar", "src", "racecar", "scripts",
                   "sim_room12.csv")
REPORT = os.path.join(HERE, "probe_follow.txt")
START = time.time()
RECORDS = []


def log(text):
    print("[probe] %7.2f s  %s" % (time.time() - START, text), flush=True)


def poses_from(path):
    out = []
    with open(path, "r") as fh:
        for row in csv.reader(fh):
            if len(row) != 4:
                continue
            pose = PoseStamped()
            pose.header.frame_id = "map"
            pose.pose.position.x = float(row[0])
            pose.pose.position.y = float(row[1])
            pose.pose.position.z = float(row[2])
            pose.pose.orientation.w = float(row[3])
            out.append(pose)
    return out


class Probe(Node):
    def __init__(self, phases):
        super().__init__("follow_waypoints_probe")
        self.client = ActionClient(self, FollowWaypoints, "follow_waypoints")
        self.phases = phases
        self.index = -1
        self.cur = None
        self.create_timer(400.0, self.timed_out)
        self.next_phase()

    def timed_out(self):
        log("TIMEOUT - reporting what there is")
        self.report()
        rclpy.shutdown()

    def next_phase(self):
        self.index += 1
        if self.index >= len(self.phases):
            self.report()
            rclpy.shutdown()
            return
        label, poses = self.phases[self.index]
        self.cur = {"label": label, "poses": len(poses), "sent": time.time(),
                    "accept": None, "result": None, "fb": [], "dist": {},
                    "missed": None, "seen": set()}
        RECORDS.append(self.cur)
        log("%s: sending %d pose(s)" % (label, len(poses)))
        goal = FollowWaypoints.Goal()
        goal.poses = poses
        if not self.client.wait_for_server(timeout_sec=20.0):
            log("%s: no action server" % label)
            self.next_phase()
            return
        future = self.client.send_goal_async(goal, feedback_callback=self.on_feedback)
        future.add_done_callback(self.on_accept)

    def on_accept(self, future):
        handle = future.result()
        self.cur["accept"] = time.time()
        log("%s: accepted=%s after %.2f s"
            % (self.cur["label"], bool(handle.accepted), self.cur["accept"] - self.cur["sent"]))
        if not handle.accepted:
            self.next_phase()
            return
        handle.get_result_async().add_done_callback(self.on_result)

    def on_feedback(self, message):
        feedback = message.feedback
        current = feedback.current_waypoint
        self.cur["seen"].add(current)
        self.cur["fb"].append((time.time(), current))
        if len(self.cur["fb"]) <= 2:
            log("%s: feedback %d  current_waypoint=%d" % (self.cur["label"], len(self.cur["fb"]),
                                                          current))

    def on_result(self, future):
        result = future.result().result
        self.cur["result"] = time.time()
        self.cur["missed"] = list(getattr(result, "missed_waypoints", []))
        log("%s: result after %.2f s  missed_waypoints=%s  feedbacks=%d"
            % (self.cur["label"], self.cur["result"] - self.cur["sent"], self.cur["missed"],
               len(self.cur["fb"])))
        self.next_phase()

    def report(self):
        print("", flush=True)
        print("[probe] ===== summary =====", flush=True)
        lines = []
        for index, record in enumerate(RECORDS, 1):
            accept = ("%.2f s" % (record["accept"] - record["sent"])
                      if record["accept"] else "NO")
            result = ("%.2f s" % (record["result"] - record["sent"])
                      if record["result"] else "NONE")
            line = ("G%d  %-28s poses=%d  accepted=%-7s result=%-9s feedbacks=%-5d "
                    "waypoints_seen=%s missed=%s"
                    % (index, record["label"], record["poses"], accept, result,
                       len(record["fb"]), sorted(record["seen"]), record["missed"]))
            print("[probe] " + line, flush=True)
            lines.append(line + "\n")
        with open(REPORT, "w", encoding="utf-8", newline="\n") as out:
            out.write("times are relative to that goal's own send\n\n")
            for line in lines:
                out.write(line)


def main():
    full = poses_from(CSV)
    log("read %d poses from %s" % (len(full), CSV))
    phases = [("G1 nine poses, lap 1", full),
              ("G2 the same nine again", full)]
    rclpy.init(args=sys.argv[1:])
    probe = Probe(phases)
    try:
        rclpy.spin(probe)
    except Exception as error:  # noqa: BLE001 - shutdown from inside a callback
        log("spin ended: %r" % (error,))
    log("done")


if __name__ == "__main__":
    main()
