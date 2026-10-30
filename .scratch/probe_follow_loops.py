# -*- coding: utf-8 -*-
"""Is `number_of_loops` the lap feature the mission scripts hand-rolled?

One FollowWaypoints goal, nine poses, number_of_loops=1. If the follower really
walks the list twice, the feedback's current_waypoint goes 0..8 and then starts
over at 0 - which is what the racecar scripts do by resending goals (and lose).

The follower's own counter starts at 0 and compares after each pass
(nav2_waypoint_follower/src/waypoint_follower.cpp:357-372), so number_of_loops=1
should mean two passes.
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
REPORT = os.path.join(HERE, "probe_follow_loops.txt")
START = time.time()
LOOPS = int(os.environ.get("PROBE_LOOPS", "1"))


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
    def __init__(self, poses):
        super().__init__("follow_loops_probe")
        self.client = ActionClient(self, FollowWaypoints, "follow_waypoints")
        self.poses = poses
        self.sent = None
        self.accept = None
        self.sequence = []
        self.count = 0
        self.create_timer(500.0, self.timed_out)
        self.send()

    def timed_out(self):
        log("TIMEOUT - reporting what there is")
        self.report()
        rclpy.shutdown()

    def send(self):
        self.sent = time.time()
        goal = FollowWaypoints.Goal()
        goal.poses = self.poses
        goal.number_of_loops = LOOPS
        log("sending %d poses with number_of_loops=%d" % (len(self.poses), LOOPS))
        if not self.client.wait_for_server(timeout_sec=20.0):
            log("no action server")
            rclpy.shutdown()
            return
        future = self.client.send_goal_async(goal, feedback_callback=self.on_feedback)
        future.add_done_callback(self.on_accept)

    def on_accept(self, future):
        handle = future.result()
        self.accept = time.time()
        log("accepted=%s after %.2f s" % (bool(handle.accepted), self.accept - self.sent))
        handle.get_result_async().add_done_callback(self.on_result)

    def on_feedback(self, message):
        current = message.feedback.current_waypoint
        self.count += 1
        if not self.sequence or self.sequence[-1][1] != current:
            self.sequence.append((round(time.time() - self.sent, 1), current))
            log("feedback %d  current_waypoint=%d" % (self.count, current))

    def on_result(self, future):
        result = future.result().result
        when = time.time()
        log("result after %.2f s  missed_waypoints=%s  feedbacks=%d"
            % (when - self.sent, list(getattr(result, "missed_waypoints", [])), self.count))
        self.report(when)
        rclpy.shutdown()

    def report(self, when=None):
        print("", flush=True)
        print("[probe] ===== summary =====", flush=True)
        line = ("number_of_loops=%d  poses=%d  result=%s  feedbacks=%d  waypoint changes=%s"
                % (LOOPS, len(self.poses),
                   ("%.2f s" % (when - self.sent)) if when else "NONE", self.count,
                   self.sequence))
        print("[probe] " + line, flush=True)
        with open(REPORT, "w", encoding="utf-8", newline="\n") as out:
            out.write(line + "\n")
            passes = sum(1 for i, item in enumerate(self.sequence)
                         if item[1] == 0 and (i == 0 or self.sequence[i - 1][1] != 0))
            out.write("times are relative to the send; passes seen (back at waypoint 0): %d\n"
                      % passes)


def main():
    poses = poses_from(CSV)
    rclpy.init(args=sys.argv[1:])
    probe = Probe(poses)
    try:
        rclpy.spin(probe)
    except Exception as error:  # noqa: BLE001 - shutdown from inside a callback
        log("spin ended: %r" % (error,))
    log("done")


if __name__ == "__main__":
    main()
