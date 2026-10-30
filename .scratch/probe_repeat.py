# -*- coding: utf-8 -*-
"""Why does the second goal of the same route come back in 0.1 s?

Four goals in one process, against one live stack:

  G1  sim_room12.csv, all 9 poses         the real lap (~73 s in evidence 77)
  G2  the very same 9 poses again         evidence 77: 0.1 s, 9/9 status=1
  G3  one pose, not the one just reached  is *any* second goal instant?
  G4  the same single pose again          is it the identity of the goal?

For each goal: accept wall time, how many feedbacks came back, every distinct
distance_remaining with its multiplicity, the result wall time, error_code and
the waypoint statuses. The answer decides between "identical goal resent" and
"any second goal in one client" - and between nav2 and this shim as the place
the fake result is made.
"""
from __future__ import annotations

import csv
import os
import sys
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import NavigateThroughPoses
from geometry_msgs.msg import PoseStamped

HERE = os.path.dirname(os.path.abspath(__file__))
STEM = os.path.dirname(HERE)
CSV = os.path.join(STEM, "code", "Desktop", "racecar", "src", "racecar", "scripts",
                   "sim_room12.csv")
REPORT = os.path.join(HERE, "probe_repeat.txt")
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
        super().__init__("repeat_goal_probe")
        self.client = ActionClient(self, NavigateThroughPoses, "navigate_through_poses")
        self.phases = phases
        self.index = -1
        self.cur = None
        self.create_timer(420.0, self.timed_out)
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
                    "code": None, "status": None}
        RECORDS.append(self.cur)
        log("%s: sending %d pose(s)" % (label, len(poses)))
        goal = NavigateThroughPoses.Goal()
        goal.poses.goals = poses
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
        distance = feedback.distance_remaining
        key = round(distance, 2)
        self.cur["dist"][key] = self.cur["dist"].get(key, 0) + 1
        self.cur["fb"].append((time.time(), key))
        if len(self.cur["fb"]) <= 3:
            log("%s: feedback %d  distance_remaining=%.2f  poses_remaining=%d"
                % (self.cur["label"], len(self.cur["fb"]), distance,
                   getattr(feedback, "number_of_poses_remaining", -1)))

    def on_result(self, future):
        result = future.result().result
        self.cur["result"] = time.time()
        self.cur["code"] = getattr(result, "error_code", None)
        self.cur["status"] = sorted({s.waypoint_status
                                     for s in getattr(result, "waypoint_statuses", [])})
        log("%s: result after %.2f s  error_code=%s  statuses=%s  feedbacks=%d"
            % (self.cur["label"], self.cur["result"] - self.cur["sent"], self.cur["code"],
               self.cur["status"], len(self.cur["fb"])))
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
                    "code=%s status=%s distinct_distance=%s"
                    % (index, record["label"], record["poses"], accept, result,
                       len(record["fb"]), record["code"], record["status"],
                       sorted(record["dist"])))
            print("[probe] " + line, flush=True)
            lines.append(line + "\n")
        with open(REPORT, "w", encoding="utf-8", newline="\n") as out:
            out.write("goal accepted/result times are relative to that goal's own send\n\n")
            for line in lines:
                out.write(line)


def main():
    full = poses_from(CSV)
    log("read %d poses from %s" % (len(full), CSV))
    phases = [("G1 full list, lap 1", full),
              ("G2 the same list again", full),
              ("G3 one pose = waypoint 1", [full[0]]),
              ("G4 that same one pose again", [full[0]])]
    rclpy.init(args=sys.argv[1:])
    probe = Probe(phases)
    try:
        rclpy.spin(probe)
    except Exception as error:  # noqa: BLE001 - shutdown from inside a callback
        log("spin ended: %r" % (error,))
    log("done")


if __name__ == "__main__":
    main()
