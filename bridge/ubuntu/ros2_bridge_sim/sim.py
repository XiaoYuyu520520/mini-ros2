"""A differential drive car simulated on top of the in-process graph.

The car subscribes to ``/cmd_vel`` and publishes ``/odom``, ``/scan``, ``/tf``,
``/tf_static``, ``/map`` and ``/clock``.  Sim time starts at 0 and advances in
fixed steps, so the same seed plus the same commands replay identically.

Like a real base, the car stops when the commands stop: a ``/cmd_vel`` that
nobody refreshes within :data:`DEFAULT_CMD_TIMEOUT` seconds is dropped rather
than integrated for ever (see :meth:`SimCar.check_cmd_watchdog`).
"""

from __future__ import annotations

import logging
import math
import threading
import time

from . import messages as msg
from .graph import Graph, Message
from .world import DEFAULT_BEAMS, DEFAULT_RANGE_MAX, DEFAULT_RANGE_MIN, World

LOG = logging.getLogger("ros2_bridge_sim.sim")

#: How long a command that nobody refreshes stays valid.  This is the sim
#: counterpart of `velocity_smoother`'s `cmd_vel_timeout`: the bridge, nav2 and
#: a hand-typed `ros2 topic pub` all publish well inside it, so the only thing
#: it ever stops is a command whose publisher has gone away.
DEFAULT_CMD_TIMEOUT = 0.5

FRAME_ODOM = "odom"
FRAME_BASE = "base_link"
FRAME_LASER = "laser"
FRAME_MAP = "map"

TOPIC_CMD_VEL = "/cmd_vel"
TOPIC_ODOM = "/odom"
TOPIC_SCAN = "/scan"
TOPIC_TF = "/tf"
TOPIC_TF_STATIC = "/tf_static"
TOPIC_MAP = "/map"
TOPIC_CLOCK = "/clock"

#: Where the lidar sits relative to ``base_link``.
LIDAR_TRANSLATION = (0.10, 0.0, 0.0)

#: Topics the simulator advertises (name, type).
SIM_TOPICS = (
    (TOPIC_CMD_VEL, msg.TWIST),
    (TOPIC_ODOM, msg.ODOMETRY),
    (TOPIC_SCAN, msg.LASER_SCAN),
    (TOPIC_TF, msg.TF_MESSAGE),
    (TOPIC_TF_STATIC, msg.TF_MESSAGE),
    (TOPIC_MAP, msg.OCCUPANCY_GRID),
    (TOPIC_CLOCK, msg.CLOCK),
)


class SimCar:
    """Integrates ``/cmd_vel`` into a pose and publishes the sensor topics."""

    def __init__(
        self,
        graph: Graph,
        world: World,
        *,
        rate: float = 50.0,
        scan_rate: float = 10.0,
        map_rate: float = 1.0,
        tf_static_period: float = 5.0,
        beams: int = DEFAULT_BEAMS,
        range_min: float = DEFAULT_RANGE_MIN,
        range_max: float = DEFAULT_RANGE_MAX,
        x: float = 0.0,
        y: float = 0.0,
        theta: float = 0.0,
        wall_epoch_ns: int | None = None,
        cmd_timeout: float | None = DEFAULT_CMD_TIMEOUT,
    ) -> None:
        self.graph = graph
        self.world = world
        self.rate = float(rate)
        self.scan_rate = float(scan_rate)
        self.map_rate = float(map_rate)
        self.tf_static_period = float(tf_static_period)
        self.beams = int(beams)
        #: The lidar's own limits: the rays are cast with them and ``/scan``
        #: advertises them, because a consumer that believes the wrong range
        #: mis-reads the returns.  An ``inf`` beam means "nothing inside
        #: *range_max*", so a world whose walls are further away than that is
        #: invisible - every beam comes back ``inf`` and a SLAM front end fed
        #: those scans has nothing to build a map from.
        self.range_min = float(range_min)
        self.range_max = float(range_max)

        self.x, self.y, self.theta = float(x), float(y), float(theta)
        self.linear = 0.0
        self.angular = 0.0

        #: Seconds a command stays valid without being refreshed; `None` turns
        #: the watchdog off.  A test that models an ideal integrator - "hold
        #: this speed for a second of simulated time" - wants `None`.
        self.cmd_timeout = None if cmd_timeout is None else float(cmd_timeout)
        #: When the current command was last set or held, on the monotonic clock.
        self._cmd_stamp = time.monotonic()
        self._cmd_hold = False
        #: How many commands the watchdog has dropped, for callers that want to
        #: assert on it instead of watching the pose.
        self.watchdog_trips = 0

        #: Wall clock instant that sim time 0 maps onto.
        self.wall_epoch_ns = time.time_ns() if wall_epoch_ns is None else int(wall_epoch_ns)
        self.sim_time = 0.0
        self.step_index = 0
        self.paused = False

        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._next_scan = 0.0
        self._next_map = 0.0
        self._next_tf_static = 0.0

        self._pub_odom = graph.create_publisher(TOPIC_ODOM, msg.ODOMETRY)
        self._pub_scan = graph.create_publisher(TOPIC_SCAN, msg.LASER_SCAN)
        self._pub_tf = graph.create_publisher(TOPIC_TF, msg.TF_MESSAGE)
        self._pub_tf_static = graph.create_publisher(TOPIC_TF_STATIC, msg.TF_MESSAGE)
        self._pub_map = graph.create_publisher(TOPIC_MAP, msg.OCCUPANCY_GRID)
        self._pub_clock = graph.create_publisher(TOPIC_CLOCK, msg.CLOCK)

        self._sub_cmd = graph.create_subscription(
            TOPIC_CMD_VEL, msg.TWIST, self._on_cmd_vel, decode=True
        )

    # -- command input -----------------------------------------------------

    def _on_cmd_vel(self, event: Message) -> None:
        twist = event.decoded
        if not twist:
            return
        self.set_cmd(twist["linear"]["x"], twist["angular"]["z"])

    def set_cmd(self, linear: float, angular: float, *, hold: bool = False) -> None:
        """Set the commanded forward speed (m/s) and yaw rate (rad/s).

        Every call restarts the watchdog clock.  `hold=True` marks the command
        as owned by whoever set it -- the interactive console, where a human is
        at the keyboard and no publisher exists to keep re-sending -- so the
        watchdog leaves it alone until the next command arrives.
        """
        with self._lock:
            self.linear = float(linear)
            self.angular = float(angular)
            self._cmd_stamp = time.monotonic()
            self._cmd_hold = bool(hold)

    def hold_cmd(self) -> None:
        """Re-stamp the current command so the watchdog leaves it alone.

        A driver that owns the velocity across many control steps -- the sim's
        own actions, which command one speed and then watch the pose -- has to
        say so every so often.  A real behavior server does exactly this by
        publishing `/cmd_vel` at a fixed rate while its goal runs, so nothing
        here is special to the simulator.
        """
        with self._lock:
            self._cmd_stamp = time.monotonic()

    @property
    def cmd_age(self) -> float:
        """Seconds since the current command was set or last held."""
        with self._lock:
            return time.monotonic() - self._cmd_stamp

    def check_cmd_watchdog(self, now: float | None = None) -> bool:
        """Drop a command nobody has refreshed; return whether it fired.

        This is the behaviour the audit found missing: a publisher that dies
        mid-command leaves the last twist latched, and a latched twist is
        indistinguishable from a live one -- the car keeps driving off, and a
        check that only asks "did it move?" keeps saying yes.
        """
        if self.cmd_timeout is None:
            return False
        with self._lock:
            if self._cmd_hold:
                return False
            stamp = time.monotonic() if now is None else now
            age = stamp - self._cmd_stamp
            if age <= self.cmd_timeout or (self.linear == 0.0 and self.angular == 0.0):
                return False
            self.linear = 0.0
            self.angular = 0.0
            self._cmd_stamp = stamp
            self.watchdog_trips += 1
        LOG.warning(
            "cmd_vel watchdog: no command for %.2f s, stopping the car (trip %d)",
            age, self.watchdog_trips,
        )
        return True

    @property
    def cmd(self) -> tuple[float, float]:
        with self._lock:
            return (self.linear, self.angular)

    # -- state -------------------------------------------------------------

    @property
    def pose(self) -> tuple[float, float, float]:
        with self._lock:
            return (self.x, self.y, self.theta)

    def set_pose(self, x: float, y: float, theta: float | None = None) -> None:
        with self._lock:
            self.x, self.y = float(x), float(y)
            if theta is not None:
                self.theta = float(theta)

    def pause(self) -> None:
        self.paused = True

    def resume(self) -> None:
        self.paused = False

    @property
    def clock_stamp(self) -> tuple[int, int]:
        """Current sim time as ``(sec, nanosec)``."""
        sec = int(self.sim_time)
        return sec, int(round((self.sim_time - sec) * 1e9))

    @property
    def stamp_ns(self) -> int:
        """Sim clock mapped onto the wall clock, in nanoseconds since epoch."""
        return self.wall_epoch_ns + int(round(self.sim_time * 1e9))

    # -- simulation --------------------------------------------------------

    def step(self, dt: float | None = None) -> None:
        """Advance the simulation by one control step and publish what is due."""
        if dt is None:
            dt = 1.0 / self.rate
        # Before the pause check: a command that expires while the car is paused
        # has to be gone by the time it resumes, or `resume` would restart the
        # robot on a velocity its publisher sent minutes ago.
        self.check_cmd_watchdog()
        with self._lock:
            if self.paused:
                return
            self.x += self.linear * math.cos(self.theta) * dt
            self.y += self.linear * math.sin(self.theta) * dt
            self.theta = _wrap_angle(self.theta + self.angular * dt)
            self.sim_time += dt
            self.step_index += 1
            now = self.sim_time
            publish_scan = now >= self._next_scan
            publish_map = now >= self._next_map
            publish_tf_static = now >= self._next_tf_static
            if publish_scan:
                self._next_scan += 1.0 / self.scan_rate
            if publish_map:
                self._next_map += 1.0 / self.map_rate
            if publish_tf_static:
                self._next_tf_static += self.tf_static_period

        self._publish_clock()
        self._publish_odom()
        self._publish_tf()
        if publish_tf_static:
            self._publish_tf_static()
        if publish_scan:
            self._publish_scan()
        if publish_map:
            self._publish_map()

    def start(self) -> None:
        """Run :meth:`step` on a fixed rate background thread."""
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="sim-car", daemon=True)
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout)
            self._thread = None

    def _run(self) -> None:
        period = 1.0 / self.rate
        deadline = time.monotonic()
        while not self._stop.is_set():
            self.step(period)
            deadline += period
            delay = deadline - time.monotonic()
            if delay > 0:
                self._stop.wait(delay)
            else:
                deadline = time.monotonic()

    # -- publishers --------------------------------------------------------

    def _header(self, frame_id: str) -> dict:
        sec, nanosec = self.clock_stamp
        return msg.make_header(frame_id, sec, nanosec)

    def _publish_clock(self) -> None:
        sec, nanosec = self.clock_stamp
        self._pub_clock.publish(message={"clock": msg.make_time(sec, nanosec)}, stamp_ns=self.stamp_ns)

    def _publish_odom(self) -> None:
        linear, angular = self.cmd
        odometry = {
            "header": self._header(FRAME_ODOM),
            "child_frame_id": FRAME_BASE,
            "pose": {
                "pose": msg.make_pose(self.x, self.y, self.theta),
                "covariance": msg.diagonal36(1e-3, 1e-3, 0.0, 0.0, 0.0, 1e-3),
            },
            "twist": {
                "twist": msg.make_twist(linear, angular),
                "covariance": msg.diagonal36(1e-3, 1e-3, 0.0, 0.0, 0.0, 1e-3),
            },
        }
        self._pub_odom.publish(message=odometry, stamp_ns=self.stamp_ns)

    def _publish_scan(self) -> None:
        angle_min, angle_max, increment = World.scan_geometry(self.beams)
        ranges = self.world.scan(self.x, self.y, self.theta, beams=self.beams,
                                 range_min=self.range_min, range_max=self.range_max)
        scan = {
            "header": self._header(FRAME_LASER),
            "angle_min": angle_min,
            "angle_max": angle_max,
            "angle_increment": increment,
            "time_increment": 0.0,
            "scan_time": 1.0 / self.scan_rate,
            "range_min": self.range_min,
            "range_max": self.range_max,
            "ranges": ranges,
            "intensities": [0.0] * self.beams,
        }
        self._pub_scan.publish(message=scan, stamp_ns=self.stamp_ns)

    def _publish_tf(self) -> None:
        transform = msg.make_transform_stamped(
            parent=FRAME_ODOM,
            child=FRAME_BASE,
            x=self.x,
            y=self.y,
            yaw=self.theta,
            sec=self.clock_stamp[0],
            nanosec=self.clock_stamp[1],
        )
        self._pub_tf.publish(message={"transforms": [transform]}, stamp_ns=self.stamp_ns)

    def _publish_tf_static(self) -> None:
        transform = msg.make_transform_stamped(
            parent=FRAME_BASE,
            child=FRAME_LASER,
            x=LIDAR_TRANSLATION[0],
            y=LIDAR_TRANSLATION[1],
            yaw=0.0,
            sec=0,
            nanosec=0,
        )
        self._pub_tf_static.publish(message={"transforms": [transform]}, stamp_ns=self.stamp_ns)

    def _publish_map(self) -> None:
        data = self.world.occupancy_data()
        grid = {
            "header": self._header(FRAME_MAP),
            "info": {
                "map_load_time": msg.make_time(0, 0),
                "resolution": self.world.resolution,
                "width": self.world.cells_x,
                "height": self.world.cells_y,
                "origin": msg.make_pose(self.world.origin_x, self.world.origin_y, 0.0),
            },
            "data": data,
        }
        self._pub_map.publish(message=grid, stamp_ns=self.stamp_ns)


class GoalDriver:
    """Simple rotate-then-drive controller that drives the car to a goal."""

    def __init__(
        self,
        graph: Graph,
        goal: tuple[float, float],
        *,
        tolerance: float = 0.25,
        linear: float = 0.6,
        angular: float = 1.8,
        angular_gain: float = 2.0,
        distance_gain: float = 0.8,
    ) -> None:
        self.graph = graph
        self.goal = (float(goal[0]), float(goal[1]))
        self.tolerance = tolerance
        self.max_linear = linear
        self.max_angular = angular
        self.angular_gain = angular_gain
        self.distance_gain = distance_gain
        self.reached = False
        self._pose = (0.0, 0.0, 0.0)
        self._pub = graph.create_publisher(TOPIC_CMD_VEL, msg.TWIST)
        self._sub = graph.create_subscription(TOPIC_ODOM, msg.ODOMETRY, self._on_odom, decode=True)

    def _on_odom(self, event: Message) -> None:
        pose = event.decoded["pose"]["pose"]
        self._pose = (pose["position"]["x"], pose["position"]["y"], msg.quaternion_to_yaw(pose["orientation"]))

    def tick(self) -> bool:
        """Publish one velocity command; returns True once the goal is reached."""
        x, y, theta = self._pose
        dx, dy = self.goal[0] - x, self.goal[1] - y
        distance = math.hypot(dx, dy)
        if distance <= self.tolerance:
            self.reached = True
            self._pub.publish(message=msg.make_twist(0.0, 0.0))
            return True
        heading_error = _wrap_angle(math.atan2(dy, dx) - theta)
        if abs(heading_error) > 0.4:
            command = (0.0, _clamp(self.angular_gain * heading_error, self.max_angular))
        else:
            command = (
                _clamp(self.distance_gain * distance, self.max_linear),
                _clamp(self.angular_gain * heading_error, self.max_angular),
            )
        self._pub.publish(message=msg.make_twist(*command))
        return False

    def stop(self) -> None:
        self._pub.publish(message=msg.make_twist(0.0, 0.0))
        self._sub.unsubscribe()
        self._pub.unadvertise()


def _wrap_angle(angle: float) -> float:
    return math.atan2(math.sin(angle), math.cos(angle))


def _clamp(value: float, limit: float) -> float:
    return max(-limit, min(limit, value))