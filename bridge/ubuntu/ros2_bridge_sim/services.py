"""Services and actions the simulated robot serves.

The bridge carries three kinds of traffic and the sim has to speak all three
before the Windows side can be said to work "like ROS2":

* topics -- `/scan`, `/odom`, `/tf`, ... published by :mod:`sim`,
* services -- one request, one response, always immediate,
* actions -- a goal that runs for a while, reports progress, and can be
  cancelled.

The services here are deliberately plain `std_srvs` types, because those are
the ones whose C++ structs the Windows compatibility layer already generates:
a caller on either side of the bridge can build and read them without the
project having to invent a message package.

Actions are described at the goal/feedback/result level (not at the DDS
`SendGoal` service level): the goal payload is a serialized `Drive_Goal`, the
feedback is a stream of `Drive_Feedback`, and the result is a `Drive_Result`.
`nav2_msgs` actions are carried exactly the same way, which is why the sim
serves `/sim/drive` under its own message package *and* `/sim/spin`,
`/sim/back_up`, `/sim/drive_on_heading` and `/sim/wait` under
`nav2_msgs/action/*`.
That is the shape a ROS2 action client works with, and it keeps the bridge
from having to model the four hidden services every ROS2 action is built on.
"""

from __future__ import annotations

import logging
import math
import time

from . import messages
from .graph import Graph
from .sim import SimCar

LOG = logging.getLogger("ros2_bridge_sim.services")

#: Services, by the names a `ros2 service list` on either side will show.
SERVICE_PAUSE = "/sim/pause"
SERVICE_RESUME = "/sim/resume"
SERVICE_STATUS = "/sim/status"
SERVICE_SET_PAUSED = "/sim/set_paused"
SERVICE_RESET = "/sim/reset"
# The two nav2 services the robot answers. `SetInitialPose` is how a caller
# says "you are actually here" - the one thing a localization stack cannot work
# out on its own - and `Toggle` is how it says "stop".
SERVICE_SET_INITIAL_POSE = "/sim/set_initial_pose"
SERVICE_ENABLE = "/sim/enable"

#: `ros2 action list` shows this one.
ACTION_DRIVE = "/sim/drive"

# The same robot, commanded with the *upstream* nav2 action types instead of
# the sim's own `Drive`. They are advertised so the cross-machine action path is
# exercised with the messages a robot that runs nav2 on board really speaks: a
# caller on the Windows machine runs
#
#     ros2 action send_goal /sim/spin nav2_msgs/action/Spin target_yaw=1.57
#
# and the car turns. Which machine serves a behavior and which one asks for it
# is the whole of the difference the bridge is hiding.
ACTION_SPIN = "/sim/spin"
ACTION_BACK_UP = "/sim/back_up"
ACTION_DRIVE_ON_HEADING = "/sim/drive_on_heading"
ACTION_WAIT = "/sim/wait"

#: How fast the simulated robot turns in place, in rad/s.
SPIN_SPEED = 1.0

#: How often the behaviors above wake up, in seconds - the same 20 Hz
#: `behavior_server` runs its own cycle at.
BEHAVIOR_PERIOD = 0.05

#: Each nav2 action numbers its own error codes (`nav2_msgs/action/<Name>.action`
#: gives every one of them a block of its own), so they are a table rather than
#: one `ERROR_TIMEOUT` - 701 is Spin's timeout and nothing else's.
ERROR_CODES = {
    ACTION_SPIN: {"timeout": 701, "invalid_input": 700},
    ACTION_BACK_UP: {"timeout": 711, "invalid_input": 713},
    ACTION_DRIVE_ON_HEADING: {"timeout": 721, "invalid_input": 724},
    ACTION_WAIT: {"timeout": 741, "invalid_input": 740},
}

#: The three action types this module serves with nav2 messages.
NAV2_ACTIONS = {
    ACTION_SPIN: (messages.SPIN, SPIN_SPEED),
    ACTION_BACK_UP: (messages.BACK_UP, None),
    ACTION_DRIVE_ON_HEADING: (messages.DRIVE_ON_HEADING, None),
    ACTION_WAIT: (messages.WAIT, None),
}

EMPTY = "std_srvs/srv/Empty"
TRIGGER = "std_srvs/srv/Trigger"
SET_BOOL = "std_srvs/srv/SetBool"
SET_INITIAL_POSE = "nav2_msgs/srv/SetInitialPose"
TOGGLE = "nav2_msgs/srv/Toggle"
DRIVE = "ros2_bridge_sim/action/Drive"

#: Speed the drive action commands, in m/s.
DRIVE_SPEED = 0.25

#: How often the drive action reports progress, in seconds.
DRIVE_PERIOD = 0.2


def duration_seconds(value: dict | None) -> float:
    """A `builtin_interfaces/Duration` dict as seconds."""
    if not value:
        return 0.0
    return float(value.get("sec", 0)) + float(value.get("nanosec", 0)) * 1e-9


def make_duration(seconds: float) -> dict:
    """Seconds as a `builtin_interfaces/Duration` dict.

    `nanosec` is unsigned and has to stay below a second, so the rounding at the
    boundary carries into `sec` rather than sending 1000000000 to the other
    machine - which a caller would read back as a duration it cannot represent.
    """
    seconds = max(0.0, float(seconds))
    whole = int(seconds)
    nanos = int(round((seconds - whole) * 1e9))
    if nanos >= 1000000000:
        whole += 1
        nanos -= 1000000000
    return {"sec": whole, "nanosec": nanos}


def describe(car: SimCar) -> dict:
    """One-line snapshot of the robot, as a `std_srvs/srv/Trigger` response."""
    x, y, theta = car.pose
    linear, angular = car.cmd
    return {
        "success": True,
        "message": "x=%.3f y=%.3f theta=%.3f cmd=(%.2f, %.2f) paused=%s"
        % (x, y, theta, linear, angular, "true" if car.paused else "false"),
    }


def register(graph: Graph, car: SimCar, agent) -> None:
    """Advertise (and implement) the sim's services and actions on `agent`."""

    def pause(_request: dict) -> dict:
        car.pause()
        LOG.info("service %s: paused", SERVICE_PAUSE)
        return describe(car)

    def resume(_request: dict) -> dict:
        car.resume()
        LOG.info("service %s: resumed", SERVICE_RESUME)
        return describe(car)

    def status(_request: dict) -> dict:
        return describe(car)

    def reset(_request: dict) -> dict:
        car.set_cmd(0.0, 0.0)
        car.set_pose(0.0, 0.0, 0.0)
        car.resume()
        LOG.info("service %s: back to the origin", SERVICE_RESET)
        return describe(car)

    def set_paused(request: dict) -> dict:
        # The one service here whose request carries a field, which is what a
        # caller that has to build a payload cares about.
        if request.get("data"):
            car.pause()
        else:
            car.resume()
        LOG.info("service %s: paused=%s", SERVICE_SET_PAUSED, request.get("data"))
        return describe(car)

    graph.create_service(SERVICE_PAUSE, EMPTY, pause)
    graph.create_service(SERVICE_RESUME, EMPTY, resume)
    graph.create_service(SERVICE_STATUS, TRIGGER, status)
    graph.create_service(SERVICE_SET_PAUSED, SET_BOOL, set_paused)
    graph.create_service(SERVICE_RESET, TRIGGER, reset)

    def set_initial_pose(request: dict) -> dict:
        """Teleport the robot to the pose the caller believes it is at."""
        # `PoseWithCovarianceStamped` is three messages deep: the stamp, the
        # `PoseWithCovariance` around the pose, and the `Pose` itself. Reading
        # one level short is how this quietly teleported the robot to (0, 0).
        stamped = request.get("pose") or {}
        with_covariance = stamped.get("pose") or {}
        inner = with_covariance.get("pose") or {}
        position = inner.get("position") or {}
        orientation = inner.get("orientation") or {}
        # Only a full quaternion names a heading; anything less is left alone
        # rather than read as a zero, which would silently turn the robot.
        yaw = messages.quaternion_to_yaw(orientation) if "w" in orientation else None
        car.set_pose(float(position.get("x", 0.0)), float(position.get("y", 0.0)), yaw)
        LOG.info(
            "service %s: pose=%.3f,%.3f yaw=%s",
            SERVICE_SET_INITIAL_POSE,
            position.get("x", 0.0), position.get("y", 0.0),
            "unchanged" if yaw is None else "%.3f" % yaw,
        )
        return {}

    def enable(request: dict) -> dict:
        """The `nav2_msgs/srv/Toggle` shape of pause/resume."""
        enabled = bool(request.get("enable"))
        car.resume() if enabled else car.pause()
        LOG.info("service %s: enable=%s", SERVICE_ENABLE, enabled)
        return {
            "success": True,
            "message": "driving enabled" if enabled else "driving disabled",
        }

    graph.create_service(SERVICE_SET_INITIAL_POSE, SET_INITIAL_POSE, set_initial_pose)
    graph.create_service(SERVICE_ENABLE, TOGGLE, enable)

    graph.create_action(ACTION_DRIVE, DRIVE)

    def drive(goal: dict, feedback, cancel) -> tuple[dict | None, str]:
        """Drive `distance` metres along the current heading, reporting progress."""
        distance = float(goal.get("distance", 0.0))
        if distance == 0.0:
            return {"travelled": 0.0, "success": True}, ""
        # Straight-line motion along whatever heading the robot has right now:
        # the sim integrates `cmd` as a unicycle, so a pure forward command
        # keeps the heading and makes the travelled distance easy to measure.
        start_x, start_y, _ = car.pose
        target = abs(distance)
        speed = math.copysign(DRIVE_SPEED, distance)
        car.set_cmd(speed, 0.0)
        # A goal that cannot make progress (the robot is paused, or wedged
        # against a wall it cannot see) must still end: bound the run by twice
        # the time the distance should take, plus a little slack.
        deadline = time.perf_counter() + 2.0 * target / DRIVE_SPEED + 5.0
        # A cancelled goal is still a result the caller can read - it says how
        # far the robot got before it stopped - so the loop keeps this up to
        # date and the cancel branch returns it rather than nothing.
        travelled = 0.0
        try:
            while True:
                if cancel.is_set():
                    LOG.info("action %s: cancelled after %.2f m", ACTION_DRIVE, travelled)
                    return {"travelled": travelled, "success": False}, "cancelled"
                time.sleep(DRIVE_PERIOD)
                # Still driving: the goal loop owns the velocity, and this is
                # what tells the watchdog so. Without it the watchdog would
                # read a 2 m drive at 0.25 m/s as "the publisher died 8 s ago".
                car.hold_cmd()
                x, y, _ = car.pose
                travelled = math.hypot(x - start_x, y - start_y)
                feedback({"travelled": travelled, "remaining": max(0.0, target - travelled)})
                if travelled >= target:
                    LOG.info("action %s: reached %.2f m", ACTION_DRIVE, travelled)
                    return {"travelled": travelled, "success": True}, ""
                if time.perf_counter() > deadline:
                    LOG.warning("action %s: gave up after %.2f m", ACTION_DRIVE, travelled)
                    return {"travelled": travelled, "success": False}, "timed out"
        finally:
            # The sim latches the last command, so stopping has to be explicit
            # or the robot keeps driving into the next test.
            car.set_cmd(0.0, 0.0)

    # ------------------------------------------------------------------
    # The nav2 behaviors.
    #
    # Same shape as `drive` above - a blocking loop that reports progress and
    # returns a result - because that is the shape the bridge hands to the
    # robot: the goal arrives as CDR, the handler is handed the decoded dict,
    # and whatever it returns is encoded back with the action's own result type.
    # ------------------------------------------------------------------

    def result_of(elapsed: float, code: int, message: str) -> dict:
        return {
            "total_elapsed_time": make_duration(elapsed),
            "error_code": code,
            "error_msg": message,
        }

    def spin(goal: dict, feedback, cancel) -> tuple[dict | None, str]:
        """Turn in place to `target_yaw`, reporting the angle covered so far."""
        target = float(goal.get("target_yaw", 0.0))
        allowance = duration_seconds(goal.get("time_allowance"))
        start = time.perf_counter()
        # A goal that cannot make progress (the robot is paused, or the caller
        # asked for a turn it will never finish) still has to end. Upstream
        # treats `time_allowance == 0` as "no limit"; running for ever is not an
        # option here either, so an absent allowance gets the time the turn
        # should take plus the same slack the drive action gives itself.
        deadline = start + (allowance or 2.0 * abs(target) / SPIN_SPEED + 5.0)
        car.set_cmd(0.0, math.copysign(SPIN_SPEED, target) if target else 0.0)
        turned = 0.0
        last = car.pose[2]
        try:
            while True:
                if cancel.is_set():
                    return result_of(time.perf_counter() - start, 0, ""), "cancelled"
                time.sleep(BEHAVIOR_PERIOD)
                car.hold_cmd()
                # Summing the increments rather than differencing against the
                # start angle: the heading is an angle, so a turn past +/-pi
                # would otherwise read as a turn back the other way.
                theta = car.pose[2]
                turned += math.atan2(math.sin(theta - last), math.cos(theta - last))
                last = theta
                feedback({"angular_distance_traveled": float(min(abs(turned), abs(target)))})
                if abs(turned) >= abs(target):
                    LOG.info("action %s: turned %.2f rad", ACTION_SPIN, turned)
                    return result_of(time.perf_counter() - start, 0, ""), ""
                if time.perf_counter() > deadline:
                    LOG.warning("action %s: gave up after %.2f rad", ACTION_SPIN, turned)
                    return result_of(
                        time.perf_counter() - start,
                        ERROR_CODES[ACTION_SPIN]["timeout"],
                        "Exceeded time allowance before reaching the Spin goal",
                    ), "timed out"
        finally:
            car.set_cmd(0.0, 0.0)

    def drive_along_heading(action_name: str):
        """The `BackUp` / `DriveOnHeading` handler, which upstream gives the
        same goal message: `target.x` is the signed distance to travel along
        the robot's current heading and `speed` has to agree with its sign."""
        codes = ERROR_CODES[action_name]

        def run(goal: dict, feedback, cancel) -> tuple[dict | None, str]:
            target = goal.get("target") or {}
            distance = float(target.get("x", 0.0))
            speed = float(goal.get("speed", 0.0))
            allowance = duration_seconds(goal.get("time_allowance"))
            if abs(distance) < 1e-9:
                return result_of(0.0, 0, ""), ""
            if (distance > 0.0) != (speed > 0.0):
                LOG.warning("action %s: speed and distance disagree", action_name)
                return result_of(0.0, codes["invalid_input"],
                    "Speed and command sign did not match"), "speed and distance disagree"
            start = time.perf_counter()
            deadline = start + (allowance or 2.0 * abs(distance) / abs(speed) + 5.0)
            start_x, start_y, _ = car.pose
            car.set_cmd(speed, 0.0)
            travelled = 0.0
            try:
                while True:
                    if cancel.is_set():
                        return result_of(time.perf_counter() - start, 0, ""), "cancelled"
                    time.sleep(BEHAVIOR_PERIOD)
                    car.hold_cmd()
                    x, y, _ = car.pose
                    travelled = math.hypot(x - start_x, y - start_y)
                    feedback({"distance_traveled": float(min(travelled, abs(distance)))})
                    if travelled >= abs(distance):
                        LOG.info("action %s: travelled %.2f m", action_name, travelled)
                        return result_of(time.perf_counter() - start, 0, ""), ""
                    if time.perf_counter() > deadline:
                        LOG.warning("action %s: gave up after %.2f m", action_name, travelled)
                        return result_of(
                            time.perf_counter() - start,
                            codes["timeout"],
                            "Exceeded time allowance before reaching the goal",
                        ), "timed out"
            finally:
                car.set_cmd(0.0, 0.0)

        return run

    def wait(goal: dict, feedback, cancel) -> tuple[dict | None, str]:
        """Hold still for `time`, reporting what is left."""
        seconds = duration_seconds(goal.get("time"))
        start = time.perf_counter()
        deadline = start + seconds
        while True:
            if cancel.is_set():
                return result_of(time.perf_counter() - start, 0, ""), "cancelled"
            left = deadline - time.perf_counter()
            if left <= 0.0:
                return result_of(time.perf_counter() - start, 0, ""), ""
            feedback({"time_left": make_duration(left)})
            time.sleep(min(BEHAVIOR_PERIOD, left))

    handlers = {
        ACTION_SPIN: spin,
        ACTION_BACK_UP: drive_along_heading(ACTION_BACK_UP),
        ACTION_DRIVE_ON_HEADING: drive_along_heading(ACTION_DRIVE_ON_HEADING),
        ACTION_WAIT: wait,
    }
    for action_name, (type_name, _) in NAV2_ACTIONS.items():
        graph.create_action(action_name, type_name)
        agent.register_action(action_name, handlers[action_name])

    agent.register_action(ACTION_DRIVE, drive)
    LOG.info(
        "sim serving %d service(s) and %d action(s): %s",
        len(graph.services()),
        1 + len(NAV2_ACTIONS),
        ", ".join(service.name for service in graph.services())
        + " / "
        + ", ".join([ACTION_DRIVE] + list(NAV2_ACTIONS)),
    )
