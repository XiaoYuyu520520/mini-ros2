# -*- coding: utf-8 -*-
"""Does the CSV's third column belong in position.z, or in the quaternion's z?

Thirteen of the mission scripts build every goal like this:

    pose.pose.position.z = point["z"]
    pose.pose.orientation.w = point["w"]

The routes are four columns of x, y, z, w where the last two are the **z and w
of a unit quaternion** - `sim_room12.csv` row 5 is (0.8435, 0.5372), i.e. yaw
+2.007 rad, and row 8 is (-0.9239, 0.3827), i.e. -2.36 rad. Putting the third
column in `position.z` and leaving the quaternion at (0, 0, 0, w) throws the
heading away: every goal the planner is handed says "point along +x", or -x when
w is negative.

Part one is arithmetic and needs no stack. Part two is the measurement: send a
goal with the old mapping, then the same x/y with the fixed mapping, and watch
what the robot does about its heading.

    racecar_ctl.py nav --map live
    racecar_ctl.py run --sim-time .scratch/probe_orientation.py

The second goal is the interesting one. The robot is already standing inside the
xy tolerance when it is sent, so anything that happens next is the heading the
CSV asked for and the old code never sent it. `xy_goal_tolerance` and
`yaw_goal_tolerance` are both 1.0 in src/racecar/config/nav.yaml:138-139, so a
heading that moves by more than a radian cannot be the goal checker being
sloppy.

`PROBE_ROW` picks the waypoint; the default is row 5, whose yaw is +115 deg.
"""
from __future__ import annotations

import csv
import math
import os
import sys
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from geometry_msgs.msg import PoseStamped, PoseWithCovarianceStamped
from nav_msgs.msg import Odometry
from nav2_msgs.action import NavigateToPose

HERE = os.path.dirname(os.path.abspath(__file__))
STEM = os.path.dirname(HERE)
CSV = os.path.join(STEM, "code", "Desktop", "racecar", "src", "racecar", "scripts",
                   "sim_room12.csv")
REPORT = os.path.join(HERE, "probe_orientation.txt")
ROW = int(os.environ.get("PROBE_ROW", "4"))
SETTLE = 3.0
START = time.time()


def log(text):
    print("[probe] %7.2f s  %s" % (time.time() - START, text), flush=True)


def yaw_of(z, w, x=0.0, y=0.0):
    """Heading of a quaternion; the routes only ever describe a planar turn."""
    return math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))


def rows():
    out = []
    with open(CSV, "r") as fh:
        for row in csv.reader(fh):
            if len(row) != 4:
                continue
            out.append(tuple(float(v) for v in row))
    return out


def pose_for(row, fixed, frame="map"):
    pose = PoseStamped()
    pose.header.frame_id = frame
    pose.pose.position.x = row[0]
    pose.pose.position.y = row[1]
    if fixed:
        pose.pose.orientation.z = row[2]
        pose.pose.orientation.w = row[3]
    else:
        pose.pose.position.z = row[2]
        pose.pose.orientation.w = row[3]
    return pose


class Probe(Node):
    """old mapping -> measure -> fixed mapping -> measure."""

    def __init__(self, table):
        super().__init__("orientation_probe")
        self.table = table
        self.client = ActionClient(self, NavigateToPose, "navigate_to_pose")
        self.amcl = None
        self.sent = None
        self.stage = "wait"
        self.frame = "?"
        self.measured = {}
        self.took = {}
        # `/odom` and not `/amcl_pose` for the pose that is always there: AMCL only
        # publishes when the robot has moved `update_min_d` / turned `update_min_a`
        # since its last update, so a stationary robot produces nothing at all and
        # a probe that waits for `/amcl_pose` waits for ever. Both are subscribed;
        # the sim's odom is its true pose and `map` starts on top of `odom` (the
        # map came from the sim), so the two agree to within localisation error.
        self.create_subscription(Odometry, "/odom", self.on_odom, 10)
        self.create_subscription(PoseWithCovarianceStamped, "/amcl_pose", self.on_amcl, 10)
        self.create_timer(0.2, self.step)
        self.deadline = time.time() + 420.0

    # -- inputs ---------------------------------------------------------
    def on_odom(self, message):
        pose = message.pose.pose
        self.amcl = (pose.position.x, pose.position.y,
                     yaw_of(pose.orientation.z, pose.orientation.w,
                            pose.orientation.x, pose.orientation.y))
        self.frame = "odom"

    def on_amcl(self, message):
        pose = message.pose.pose
        self.amcl = (pose.position.x, pose.position.y,
                     yaw_of(pose.orientation.z, pose.orientation.w,
                            pose.orientation.x, pose.orientation.y))

    def log_amcl(self, label):
        log("%s (%s frame): x=%.2f y=%.2f yaw=%+.2f deg"
            % ((label, self.frame) + self.amcl[:2] + (math.degrees(self.amcl[2]),)))

    # -- state machine --------------------------------------------------
    def step(self):
        if time.time() > self.deadline:
            log("TIMEOUT in stage %s" % self.stage)
            self.finish()
            return
        if self.stage == "wait":
            if self.amcl is None:
                return
            log_amcl_here = self.log_amcl
            log_amcl_here("start")
            self.stage = "old-wait"
            self.go_at = time.time() + 2.0
            return
        if self.stage == "old-wait" and time.time() >= self.go_at:
            self.send("old", fixed=False)
            return
        if self.stage == "old-settle" and time.time() >= self.go_at:
            log_amcl_here = self.log_amcl
            log_amcl_here("old ended")
            self.measured["old"] = self.amcl
            self.send("fixed", fixed=True)
            return
        if self.stage == "fixed-settle" and time.time() >= self.go_at:
            log_amcl_here = self.log_amcl
            log_amcl_here("fixed ended")
            self.measured["fixed"] = self.amcl
            self.finish()
            return

    def send(self, label, fixed):
        row = self.table[ROW]
        if not self.client.wait_for_server(timeout_sec=20.0):
            log("no navigate_to_pose server")
            self.finish()
            return
        goal = NavigateToPose.Goal()
        goal.pose = pose_for(row, fixed)
        self.stage = "%s-sent" % label
        self.sent = time.time()
        log("--- %s mapping: goal x=%.2f y=%.2f  quaternion z=%.4f w=%.4f  (yaw %+.2f deg)"
            % (label, row[0], row[1], goal.pose.pose.orientation.z,
               goal.pose.pose.orientation.w,
               math.degrees(yaw_of(goal.pose.pose.orientation.z,
                                   goal.pose.pose.orientation.w))))
        self.client.send_goal_async(goal).add_done_callback(self.on_accept)

    def on_accept(self, future):
        handle = future.result()
        label = self.stage.split("-")[0]
        if not handle.accepted:
            log("%s goal rejected" % label)
            self.finish()
            return
        log("%s goal accepted" % label)
        handle.get_result_async().add_done_callback(
            lambda fut, name=label: self.on_result(name, fut))

    def on_result(self, label, future):
        self.took[label] = time.time() - self.sent
        log("%s result after %.2f s (status=%s)" % (label, self.took[label],
                                                    future.result().status))
        self.stage = "%s-settle" % label
        self.go_at = time.time() + SETTLE

    # -- output ---------------------------------------------------------
    def finish(self):
        asked = yaw_of(self.table[ROW][2], self.table[ROW][3])
        old = self.measured.get("old")
        fixed = self.measured.get("fixed")
        lines = ["CSV: %s" % CSV, "",
                 "row   x       y       col3(z)  col4(w)   yaw asked for   yaw the old mapping sends"]
        for i, row in enumerate(self.table):
            lines.append("%3d  %6.2f  %6.2f   %7.4f  %7.4f   %+8.2f deg     %+8.2f deg"
                         % (i, row[0], row[1], row[2], row[3],
                            math.degrees(yaw_of(row[2], row[3])),
                            math.degrees(yaw_of(0.0, row[3]))))
        lines += ["", "live measurement, waypoint row %d (x=%.2f y=%.2f, yaw asked %+.2f deg)"
                  % (ROW, self.table[ROW][0], self.table[ROW][1], math.degrees(asked))]
        for label, text in (("old", "position.z = col3, quaternion (0,0,0,col4)"),
                            ("fixed", "quaternion (0,0,col3,col4)")):
            got = self.measured.get(label)
            if got is None:
                lines.append("  %-5s  %-45s NO MEASUREMENT" % (label, text))
                continue
            err = math.degrees(abs(math.atan2(math.sin(got[2] - asked),
                                              math.cos(got[2] - asked))))
            lines.append("  %-5s  %-45s ended x=%.2f y=%.2f yaw=%+.2f deg  "
                         "(%.2f deg off what the CSV asked, %.1f s)"
                         % (label, text, got[0], got[1], math.degrees(got[2]), err,
                            self.took.get(label, float("nan"))))
        lines += ["", "both tolerances are 1.0 rad (57.3 deg) in "
                      "src/racecar/config/nav.yaml:138-139"]
        text = "\n".join(lines)
        print("", flush=True)
        print("[probe] ===== summary =====", flush=True)
        for line in lines:
            print("[probe] " + line, flush=True)
        with open(REPORT, "w", encoding="utf-8", newline="\n") as out:
            out.write(text + "\n")
        rclpy.shutdown()


def main():
    table = rows()
    rclpy.init(args=sys.argv[1:])
    probe = Probe(table)
    try:
        rclpy.spin(probe)
    except Exception as error:  # noqa: BLE001 - shutdown from inside a callback
        log("spin ended: %r" % (error,))
    log("done")


if __name__ == "__main__":
    main()
