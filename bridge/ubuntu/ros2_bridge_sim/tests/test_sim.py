#!/usr/bin/env python3
"""Unit tests for the simulated car, the world and the lidar.

Run under pytest or standalone::

    python tests/test_sim.py
"""

from __future__ import annotations

import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from ros2_bridge_sim import messages as m  # noqa: E402
from ros2_bridge_sim.graph import Graph  # noqa: E402
from ros2_bridge_sim.sim import (  # noqa: E402
    DEFAULT_CMD_TIMEOUT,
    TOPIC_CMD_VEL,
    TOPIC_ODOM,
    TOPIC_SCAN,
    TOPIC_TF,
    SimCar,
)
from ros2_bridge_sim.world import (  # noqa: E402
    DEFAULT_RANGE_MAX,
    FREE,
    OCCUPIED,
    UNKNOWN,
    World,
)

BEAM_FORWARD = 180  # beams=360 spans -pi..pi, so index 180 points along +x


def run_steps(car: SimCar, count: int, dt: float = 0.02) -> None:
    for _ in range(count):
        car.step(dt)


# --------------------------------------------------------------------------
# Car kinematics
# --------------------------------------------------------------------------

def test_car_integrates_linear_cmd_vel():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0)
    graph.create_subscription(TOPIC_ODOM, m.ODOMETRY, lambda event: None)

    # goes through the graph, i.e. exactly the path a real /cmd_vel takes
    graph.publish("/cmd_vel", m.TWIST, message=m.make_twist(1.0, 0.0))
    assert car.cmd == (1.0, 0.0)

    run_steps(car, 50, 0.02)  # 50 * 0.02 s = 1 s at 1 m/s
    x, y, theta = car.pose
    assert abs(x - 1.0) < 1e-9, x
    assert abs(y) < 1e-9, y
    assert abs(theta) < 1e-9, theta
    assert abs(car.sim_time - 1.0) < 1e-9, car.sim_time


def test_car_integrates_angular_cmd_vel():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0)

    graph.publish("/cmd_vel", m.TWIST, message=m.make_twist(0.0, 1.0))
    assert car.cmd == (0.0, 1.0)

    run_steps(car, 25, 0.02)  # 0.5 s at 1 rad/s
    x, y, theta = car.pose
    assert abs(theta - 0.5) < 1e-9, theta
    assert abs(x) < 1e-9 and abs(y) < 1e-9


def test_car_drives_a_circle():
    """Constant linear + angular speed traces an arc of v/w."""
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=100.0)
    steps, dt = 100, 0.01  # 1 s at pi/2 rad/s -> a quarter turn
    linear, angular = 1.0, math.pi / 2

    graph.publish("/cmd_vel", m.TWIST, message=m.make_twist(linear, angular))
    run_steps(car, steps, dt)

    x, y, theta = car.pose
    assert abs(theta - angular * steps * dt) < 1e-9, theta

    # the integrator is forward Euler (x += v * cos(theta_i) * dt with the
    # pose updated before the heading), which has the closed form
    #   sum(cos(i*a)) = sin(N*a/2) * cos((N-1)*a/2) / sin(a/2)
    a = angular * dt
    sweep = math.sin(steps * a / 2) / math.sin(a / 2)
    assert abs(x - linear * dt * sweep * math.cos((steps - 1) * a / 2)) < 1e-9, x
    assert abs(y - linear * dt * sweep * math.sin((steps - 1) * a / 2)) < 1e-9, y

    radius = linear / angular
    assert abs(math.hypot(x, y) - radius * math.sqrt(2)) < 0.01  # chord of a quarter turn


def test_pause_and_resume_freeze_the_simulation():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0)
    graph.publish("/cmd_vel", m.TWIST, message=m.make_twist(1.0, 0.0))
    run_steps(car, 10)
    x_before, sim_before = car.pose[0], car.sim_time

    car.pause()
    run_steps(car, 10)
    assert car.pose[0] == x_before
    assert car.sim_time == sim_before

    car.resume()
    run_steps(car, 10)
    assert car.pose[0] > x_before
    assert car.sim_time > sim_before


def test_car_publishes_odom_scan_tf_and_map():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, scan_rate=10.0, map_rate=1.0)
    counts: dict[str, int] = {}
    for topic, type_name in ((TOPIC_ODOM, m.ODOMETRY), (TOPIC_SCAN, m.LASER_SCAN), (TOPIC_TF, m.TF_MESSAGE)):
        counts[topic] = 0
        graph.create_subscription(
            topic, type_name, lambda event, topic=topic: counts.__setitem__(topic, counts[topic] + 1)
        )

    run_steps(car, 50, 0.02)  # one second

    assert counts[TOPIC_ODOM] == 50
    assert counts[TOPIC_TF] == 50
    # ~10 Hz out of a 50 Hz loop; one second of steps lands on the boundary
    assert 9 <= counts[TOPIC_SCAN] <= 11, counts[TOPIC_SCAN]

    map_topic = graph.get_topic("/map")
    assert map_topic is not None and map_topic.publisher_count == 1
    # 1 Hz on a 50 Hz loop: one or two maps in the first second
    assert 1 <= map_topic.sequence <= 2, map_topic.sequence


def test_scan_message_is_well_formed():
    graph, world = Graph(), World()
    world.add_obstacle(3.0, 0.0, 0.2, 4.0)
    car = SimCar(graph, world, rate=50.0)
    received = []
    graph.create_subscription(TOPIC_SCAN, m.LASER_SCAN, received.append, decode=True)

    run_steps(car, 10)

    assert len(received) >= 1
    scan = received[0].decoded
    assert scan["header"]["frame_id"] == "laser"
    assert len(scan["ranges"]) == 360
    assert len(scan["intensities"]) == 360
    angle_min, angle_max, increment = World.scan_geometry(360)
    assert abs(scan["angle_min"] - angle_min) < 1e-6
    assert abs(scan["angle_increment"] - increment) < 1e-6
    assert abs((angle_max - angle_min) - 2 * math.pi) < 0.02  # a full turn
    # the message really is CDR encoded and decodable from the raw bytes too
    assert m.encode(scan, m.LASER_SCAN) == received[0].data


def test_scan_message_advertises_the_cars_own_lidar_range():
    """``/scan`` says what the rays were actually cast with.

    The numbers used to be literals - 0.15 m and 12.0 m - so a car given a
    different lidar still announced the old ones, and a consumer that trusts
    ``range_max`` to tell "no return" from "a return at the limit" read the
    message wrong.
    """
    graph = Graph()
    world = World(width=12.0, height=12.0, origin=(-6.0, -6.0))
    world.add_boundary_walls()
    car = SimCar(graph, world, rate=50.0, range_max=10.0)
    received = []
    graph.create_subscription(TOPIC_SCAN, m.LASER_SCAN, received.append, decode=True)

    run_steps(car, 10)

    scan = received[0].decoded
    assert scan["range_max"] == 10.0
    # 0.15 is a float32 field in the message, so compare to float32 precision
    assert abs(scan["range_min"] - 0.15) < 1e-6
    # the walls are 5.8 m away, the corners 5.8 * sqrt(2) = 8.2 m, and the
    # range reaches 10 m, so this time every beam returns
    assert all(value != math.inf for value in scan["ranges"])
    assert 5.7 <= scan["ranges"][BEAM_FORWARD] <= 5.9, scan["ranges"][BEAM_FORWARD]


# --------------------------------------------------------------------------
# Lidar
# --------------------------------------------------------------------------

def test_lidar_is_shorter_at_a_wall_than_in_open_space():
    world = World()
    open_ranges = world.scan(0.0, 0.0, 0.0)
    assert len(open_ranges) == 360
    assert open_ranges[BEAM_FORWARD] == math.inf  # nothing to see in an empty world
    assert all(range_value == math.inf for range_value in open_ranges)

    world.add_obstacle(2.0, 0.0, 0.2, 4.0)  # wall spanning x = 1.9 .. 2.1
    wall_ranges = world.scan(0.0, 0.0, 0.0)

    at_wall = wall_ranges[BEAM_FORWARD]
    assert 1.89 <= at_wall <= 2.01, at_wall
    assert at_wall < open_ranges[BEAM_FORWARD]
    # the beams pointing away from the wall still see nothing
    assert wall_ranges[0] == math.inf
    assert wall_ranges[359] == math.inf


def test_lidar_respects_its_range_limits():
    world = World()
    world.add_obstacle(20.0, 0.0, 1.0, 1.0)  # well past range_max (12 m)
    assert world.scan(0.0, 0.0, 0.0)[BEAM_FORWARD] == math.inf

    world.clear()
    world.add_obstacle(0.05, 0.0, 0.02, 1.0)  # closer than range_min (0.15 m)
    assert world.scan(0.0, 0.0, 0.0)[BEAM_FORWARD] == math.inf


def test_lidar_follows_the_heading():
    world = World()
    world.add_obstacle(2.0, 0.0, 0.2, 4.0)  # wall to the +x side

    facing_wall = world.scan(0.0, 0.0, 0.0)
    facing_away = world.scan(0.0, 0.0, math.pi)

    assert 1.89 <= facing_wall[BEAM_FORWARD] <= 2.01
    assert facing_away[BEAM_FORWARD] == math.inf
    assert 1.89 <= facing_away[0] <= 2.01  # beam 0 now points along +x


def test_lidar_sees_the_boundary_walls():
    world = World()
    world.populate(seed=3)
    ranges = world.scan(0.0, 0.0, 0.0, range_max=100.0)
    finite = [value for value in ranges if value != math.inf]
    assert finite, "the boundary walls should always be visible"
    # the walls sit 0.2 m thick just inside the 24 m world, so the inner face
    # straight ahead is at x = 11.8 (corners are further away: 11.8 * sqrt(2))
    assert 11.7 <= ranges[BEAM_FORWARD] <= 11.9, ranges[BEAM_FORWARD]
    assert max(finite) < 11.8 * math.sqrt(2) + 1e-6


def test_a_world_narrower_than_the_lidar_range_can_see_its_walls():
    """Why the simulator has a `--world-size`: SLAM cannot map what it cannot see.

    `World()` itself is still 24 m across, so from the middle the boundary walls
    sit 11.8 m away - inside the 12 m lidar range, but only just, and only along
    the axes; every other beam runs past a corner and comes back ``inf``.  Drive
    a few metres and the walls leave the range altogether.  A front end handed
    those scans builds an almost empty map, which is exactly what the SLAM demo
    used to produce.  Shrinking the arena to well under the range is the fix,
    and `run_sim` therefore defaults `--world-size` to `--range` (see
    `test_a_fresh_start_is_mappable_without_any_flags`).  This test pins the
    geometry itself, with the size spelled out.
    """
    world = World(width=12.0, height=12.0, origin=(-6.0, -6.0))
    world.add_boundary_walls()

    ranges = world.scan(0.0, 0.0, 0.0)

    assert len(ranges) == 360
    assert all(value != math.inf for value in ranges)
    assert 5.7 <= ranges[BEAM_FORWARD] <= 5.9, ranges[BEAM_FORWARD]
    # a corner is 5.8 * sqrt(2) away, still inside the 12 m range
    assert 8.1 <= ranges[45] <= 8.3, ranges[45]


def test_a_fresh_start_is_mappable_without_any_flags():
    """The arena default has to match the lidar range, not be twice it.

    `--world-size` used to default to 24.0 against a 12.0 m lidar, so a fresh
    `run_sim` put every boundary wall outside the range and SLAM built an
    almost empty map (evidence 66). The default now equals `--range`, which
    leaves the walls half a range away - visible from anywhere in the arena.
    """
    from ros2_bridge_sim import run_sim

    args = run_sim.parse_args([])

    assert args.world_size == args.range == DEFAULT_RANGE_MAX
    assert args.world_size / 2.0 < args.range
# --------------------------------------------------------------------------
# Occupancy grid
# --------------------------------------------------------------------------

def test_occupancy_grid_marks_obstacle_cells():
    world = World()
    assert (world.cells_x, world.cells_y) == (480, 480)
    assert world.occupancy_value(0.0, 0.0) == FREE

    world.add_obstacle(0.0, 0.0, 1.0, 1.0)

    data = world.occupancy_data()
    assert len(data) == world.cells_x * world.cells_y

    ix, iy = world.cell_index(0.0, 0.0)
    assert data[iy * world.cells_x + ix] == OCCUPIED
    assert world.occupancy_value(0.0, 0.0) == OCCUPIED
    assert world.occupancy_value(0.25, 0.25) == OCCUPIED

    # a cell well away from the obstacle stays free, and is not touched by it
    fx, fy = world.cell_index(6.0, 6.0)
    assert data[fy * world.cells_x + fx] == FREE
    assert world.occupancy_value(6.0, 6.0) == FREE

    # outside the map the cell is unknown
    assert world.occupancy_value(50.0, 50.0) == UNKNOWN


def test_occupancy_grid_supports_the_unknown_value():
    world = World()
    world.add_region(0.0, 0.0, 1.0, 1.0, UNKNOWN)
    ix, iy = world.cell_index(0.0, 0.0)
    assert world.occupancy_data()[iy * world.cells_x + ix] == UNKNOWN
    assert world.occupancy_value(0.0, 0.0) == UNKNOWN


def test_occupancy_grid_message_encodes_and_decodes():
    graph, world = Graph(), World()
    world.add_obstacle(0.0, 0.0, 1.0, 1.0)
    car = SimCar(graph, world, rate=50.0)
    received = []
    graph.create_subscription("/map", m.OCCUPANCY_GRID, received.append, decode=True)

    run_steps(car, 50)  # 1 s at 50 Hz -> one map at 1 Hz

    assert len(received) >= 1
    grid = received[0].decoded
    assert grid["header"]["frame_id"] == "map"
    assert grid["info"]["width"] == 480
    assert grid["info"]["height"] == 480
    assert abs(grid["info"]["resolution"] - 0.05) < 1e-9
    assert grid["info"]["origin"]["position"]["x"] == -12.0
    assert len(grid["data"]) == 480 * 480
    assert set(grid["data"]) <= {-1, 0, 100}

    ix, iy = world.cell_index(0.0, 0.0)
    assert grid["data"][iy * 480 + ix] == OCCUPIED
    assert m.encode(grid, m.OCCUPANCY_GRID) == received[0].data


# --------------------------------------------------------------------------
# cmd_vel watchdog
#
# The audit's first finding: `_on_cmd_vel` latched the value and the integrator
# used it for ever, so a bridge that died mid-command left the car driving and
# a check that only asked "did it move?" still passed.
# --------------------------------------------------------------------------

def test_the_default_timeout_is_half_a_second():
    # The number itself is the contract: every real publisher on this link -
    # nav2's controller, the bridge's mirror, a hand-typed `ros2 topic pub
    # --rate 10` - is an order of magnitude inside it.
    assert DEFAULT_CMD_TIMEOUT == 0.5


def test_cmd_vel_watchdog_stops_the_car_when_the_publisher_goes_away():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.2)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(1.0, 0.0))
    assert car.cmd == (1.0, 0.0)
    assert car.watchdog_trips == 0

    # Still inside the window: the command is left alone.
    assert car.check_cmd_watchdog(now=time.monotonic() + 0.05) is False
    assert car.cmd == (1.0, 0.0)

    # Past it: dropped, once.
    assert car.check_cmd_watchdog(now=time.monotonic() + 0.5) is True
    assert car.cmd == (0.0, 0.0)
    assert car.watchdog_trips == 1

    # A second sweep must not count the same death twice.
    assert car.check_cmd_watchdog(now=time.monotonic() + 5.0) is False
    assert car.watchdog_trips == 1


def test_step_drops_the_last_command_instead_of_integrating_it_for_ever():
    """The failure the audit reproduced: no publisher, car still moving."""
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.05)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(1.0, 0.0))

    car.step(0.02)
    assert car.pose[0] > 0.0

    time.sleep(0.12)  # longer than the timeout, nobody re-published
    car.step(0.02)
    x_after_trip = car.pose[0]
    assert car.cmd == (0.0, 0.0)
    assert car.watchdog_trips == 1

    run_steps(car, 25, 0.02)  # another half second of simulated time
    assert car.pose[0] == x_after_trip, "the car kept driving after the watchdog fired"


def test_a_refreshed_command_never_trips_the_watchdog():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.2)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(0.0, 0.5))

    # Half a second of real time, i.e. 2.5 x the timeout, and the command is
    # still alive because something kept saying "this is still current".
    for _ in range(5):
        time.sleep(0.1)
        car.hold_cmd()
    assert car.check_cmd_watchdog(now=time.monotonic() + 0.1) is False
    assert car.cmd == (0.0, 0.5)
    assert car.watchdog_trips == 0

    run_steps(car, 25, 0.02)
    assert car.pose[2] > 0.0, "a live command was stopped"


def test_the_watchdog_leaves_a_held_command_alone():
    # The interactive console is a human at a keyboard: there is no publisher
    # to time out, so `hold=True` exempts the command until the next one.
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.05)
    car.set_cmd(0.3, 0.0, hold=True)

    assert car.check_cmd_watchdog(now=time.monotonic() + 10.0) is False
    assert car.cmd == (0.3, 0.0)

    # ... and the next command, from anywhere, re-arms it.
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(0.3, 0.0))
    assert car.check_cmd_watchdog(now=time.monotonic() + 10.0) is True
    assert car.cmd == (0.0, 0.0)


def test_a_stale_zero_does_not_count_as_a_trip():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.05)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(0.0, 0.0))
    assert car.check_cmd_watchdog(now=time.monotonic() + 10.0) is False
    assert car.watchdog_trips == 0


def test_the_watchdog_can_be_switched_off():
    # An ideal integrator is still what a kinematics test wants.
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=None)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(1.0, 0.0))

    assert car.check_cmd_watchdog(now=time.monotonic() + 3600.0) is False
    run_steps(car, 50, 0.02)
    assert abs(car.pose[0] - 1.0) < 1e-9


def test_the_watchdog_also_fires_while_the_car_is_paused():
    graph, world = Graph(), World()
    car = SimCar(graph, world, rate=50.0, cmd_timeout=0.05)
    graph.publish(TOPIC_CMD_VEL, m.TWIST, message=m.make_twist(1.0, 0.0))
    car.pause()

    time.sleep(0.12)
    car.step(0.02)  # paused: integrates nothing, still checks the command
    assert car.cmd == (0.0, 0.0), "resuming would have restarted a dead command"
    assert car.watchdog_trips == 1


def main() -> int:
    failures = 0
    for name in sorted(globals()):
        if not name.startswith("test_"):
            continue
        function = globals()[name]
        try:
            function()
        except AssertionError as exc:
            failures += 1
            print("FAIL %s: %s" % (name, exc))
        else:
            print("PASS %s" % name)
    print("=" * 60)
    print("%d failed" % failures if failures else "all sim tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())