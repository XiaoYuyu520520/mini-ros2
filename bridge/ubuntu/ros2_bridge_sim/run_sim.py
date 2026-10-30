#!/usr/bin/env python3
"""Entry point: sim + bridge agent (+ an interactive console for the car).

Examples
--------
    python -m ros2_bridge_sim.run_sim
    python -m ros2_bridge_sim.run_sim --seed 7 --port 50051 --goal 6,-4
    python -m ros2_bridge_sim.run_sim --no-console --duration 30
"""

from __future__ import annotations

import argparse
import itertools
import json
import logging
import sys
import threading

from .agent import DEFAULT_HOST, DEFAULT_PORT, build_server, stop_server
from .graph import Graph
from . import messages
from .sim import GoalDriver, SimCar, TOPIC_CMD_VEL
from . import services
from .world import DEFAULT_BEAMS, DEFAULT_RANGE_MAX, World

LOG = logging.getLogger("ros2_bridge_sim.run_sim")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="ros2_bridge_sim",
        description="Run the simulated differential drive robot and the gRPC bridge agent.",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="bind address (default %(default)s)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="bind port (default %(default)s)")
    parser.add_argument("--seed", type=int, default=None, help="seed for the interior obstacles")
    parser.add_argument("--rate", type=float, default=50.0, help="simulation rate in Hz (default %(default)s)")
    parser.add_argument(
        "--world-size",
        type=float,
        default=12.0,
        metavar="METRES",
        help=(
            "side of the square arena in metres (default %(default)s); the boundary"
            " walls sit just inside it, so a world much wider than --range leaves"
            " the lidar with nothing to see and SLAM with an empty map. The"
            " default matches --range, so the walls sit half a range away and are"
            " visible from the middle - which is what makes a fresh start mappable"
        ),
    )
    parser.add_argument(
        "--range",
        type=float,
        default=DEFAULT_RANGE_MAX,
        metavar="METRES",
        help="lidar maximum range in metres (default %(default)s)",
    )
    parser.add_argument(
        "--beams",
        type=int,
        default=DEFAULT_BEAMS,
        metavar="N",
        help="lidar beam count (default %(default)s)",
    )
    parser.add_argument(
        "--goal",
        default=None,
        metavar="X,Y[,THETA]",
        help="optional goal pose; a simple controller drives the car there",
    )
    parser.add_argument(
        "--no-services", action="store_true",
        help="do not advertise the sim services/actions over the bridge",
    )
    parser.add_argument("--no-console", action="store_true", help="do not start the interactive console")
    parser.add_argument(
        "--subscribe",
        action="append",
        default=[],
        metavar="TOPIC[:TYPE]",
        help=(
            "extra topic to watch, repeatable; every message is logged as "
            "'[sub] TOPIC #N TYPE <- fields' (test hook for the bridge)"
        ),
    )
    parser.add_argument(
        "--duration", type=float, default=0.0, help="stop after this many seconds (0 = run until interrupted)"
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    return parser.parse_args(argv)


def parse_goal(text: str) -> tuple[float, float]:
    parts = [part for part in text.replace(";", ",").split(",") if part.strip()]
    if len(parts) < 2:
        raise argparse.ArgumentTypeError("--goal expects 'X,Y' or 'X,Y,THETA'")
    return (float(parts[0]), float(parts[1]))


def parse_subscription(spec: str) -> tuple[str, str]:
    """Split a ``--subscribe`` argument into ``(topic, type)``.

    The type half is optional: ``/probe/x:nav2_msgs/msg/TrackingFeedback``
    spells it out, ``/probe/x`` leaves it to whatever the first publisher
    announces.
    """
    name, _, type_name = spec.partition(":")
    name, type_name = name.strip(), type_name.strip()
    if not name:
        raise ValueError("--subscribe expects 'TOPIC[:TYPE]', got %r" % spec)
    return name, type_name


def summarize(decoded: dict, limit: int = 240) -> str:
    """One compact line for a decoded message, so the log stays readable."""
    text = json.dumps(decoded, default=str, ensure_ascii=False, sort_keys=True)
    return text if len(text) <= limit else text[: limit - 3] + "..."


def attach_probe_subscriptions(graph: Graph, specs: list[str]) -> list:
    """Watch extra topics on the robot side of the bridge.

    The car only subscribes to what it drives on, so a caller that pushes a
    topic *from Windows to the robot* would otherwise have nothing to land on.
    Each message is logged to stdout, which is where the acceptance runs read
    it from; a type this side has no schema for is reported with its byte
    count instead of being dropped, so a half-working handshake is still
    visible in the log.
    """
    handles = []
    for spec in specs:
        name, type_name = parse_subscription(spec)

        def callback(event, topic_name=name, counter=itertools.count(1)):
            index = next(counter)
            try:
                detail = summarize(messages.decode(event.data, event.type))
            except messages.UnsupportedType:
                detail = "%d byte(s), no schema for %s on this side" % (len(event.data), event.type)
            except Exception as exc:  # never let a probe kill the agent thread
                detail = "%d byte(s), undecodable: %s" % (len(event.data), exc)
            print("[sub] %s #%d %s <- %s" % (topic_name, index, event.type, detail), flush=True)

        handles.append(graph.create_subscription(name, type_name, callback))
    return handles


def topic_table(graph: Graph) -> str:
    rows = [("topic", "type", "pub", "sub")]
    for topic in graph.topics():
        rows.append((topic.name, topic.type or "-", str(topic.publisher_count), str(topic.subscriber_count)))
    widths = [max(len(row[col]) for row in rows) for col in range(len(rows[0]))]
    lines = []
    for index, row in enumerate(rows):
        lines.append("  ".join(cell.ljust(widths[col]) for col, cell in enumerate(row)).rstrip())
        if index == 0:
            lines.append("  ".join("-" * width for width in widths))
    return "\n".join(lines)


def run_goal_driver(car: SimCar, graph: Graph, goal: tuple[float, float], stop_event: threading.Event) -> None:
    """Drive to ``goal`` at 10 Hz until it is reached or the run stops."""
    driver = GoalDriver(graph, goal)
    LOG.info("driving to goal (%.2f, %.2f)", goal[0], goal[1])
    try:
        while not stop_event.is_set():
            if driver.tick():
                LOG.info("goal reached at pose (%.2f, %.2f, %.2f)", *car.pose)
                break
            stop_event.wait(0.1)
    finally:
        driver.stop()


def console_loop(car: SimCar, world: World, graph: Graph, stop_event: threading.Event) -> None:
    """Read commands from stdin until EOF or ``quit``."""
    help_text = ("commands: status | topics | drive <lin> <ang> (holds until the next drive/stop) | "
                 "stop | pose <x> <y> <th> | obstacle <x> <y> <w> <h> | pause | resume | quit")
    print("[console] " + help_text, flush=True)
    while not stop_event.is_set():
        try:
            line = input("sim> ")
        except (EOFError, KeyboardInterrupt):
            print()
            break
        parts = line.split()
        if not parts:
            continue
        command, args = parts[0].lower(), parts[1:]
        try:
            if command in ("quit", "exit", "q"):
                stop_event.set()
                break
            if command in ("help", "?"):
                print(help_text)
            elif command == "status":
                x, y, theta = car.pose
                linear, angular = car.cmd
                print(
                    "pose=(%.3f, %.3f, %.3f rad) cmd=(%.3f m/s, %.3f rad/s) cmd_age=%.2fs sim_time=%.2fs "
                    "steps=%d paused=%s watchdog_trips=%d topics=%d"
                    % (x, y, theta, linear, angular, car.cmd_age, car.sim_time, car.step_index,
                       car.paused, car.watchdog_trips, len(graph.topics()))
                )
            elif command == "topics":
                print(topic_table(graph))
            elif command in ("drive", "stop"):
                # A human at a keyboard is a legitimate source of "keep going":
                # there is no publisher to time out, so the command is held
                # rather than left to the watchdog.
                linear = float(args[0]) if command == "drive" else 0.0
                angular = float(args[1]) if command == "drive" else 0.0
                car.set_cmd(linear, angular, hold=command == "drive")
                print("driving with linear=%s angular=%s" % (linear, angular))
            elif command == "pose":
                car.set_pose(float(args[0]), float(args[1]), float(args[2]) if len(args) > 2 else None)
                print("pose set to %s" % (car.pose,))
            elif command == "obstacle":
                region = world.add_obstacle(float(args[0]), float(args[1]), float(args[2]), float(args[3]))
                print("added obstacle %s" % (region,))
            elif command == "pause":
                car.pause()
                print("paused")
            elif command == "resume":
                car.resume()
                print("resumed")
            else:
                print("unknown command %r" % command)
        except IndexError:
            print("%s: missing arguments -- %s" % (command, help_text))
        except ValueError as exc:
            print("%s: %s" % (command, exc))
    stop_event.set()


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
    )

    goal = parse_goal(args.goal) if args.goal else None

    graph = Graph()
    watched = attach_probe_subscriptions(graph, args.subscribe)
    world = World(
        width=args.world_size,
        height=args.world_size,
        origin=(-args.world_size / 2.0, -args.world_size / 2.0),
    )
    world.populate(seed=args.seed)
    car = SimCar(graph, world, rate=args.rate, beams=args.beams, range_max=args.range)

    server, agent, port = build_server(graph, args.host, args.port)
    car.start()
    server.start()
    if not args.no_services:
        services.register(graph, car, agent)

    print("ros2_bridge_sim: simulated robot ready")
    print("  gRPC   : %s:%d  (ros_distro=sim, agent=ros2_bridge_sim/0.1)" % (args.host, port))
    print("  world  : %.1f m x %.1f m @ %.2f m/cell, origin (%.0f, %.0f), %d obstacle(s)"
          % (world.width, world.height, world.resolution, world.origin_x, world.origin_y, len(world.regions)))
    print("  rate   : %.1f Hz (%d lidar beams, %.1f m range)"
          % (car.rate, car.beams, car.range_max))
    print("  topics :")
    print(topic_table(graph))
    if watched:
        print("  watch  :")
        for name, type_name in (parse_subscription(spec) for spec in args.subscribe):
            print("    %-24s [%s]" % (name, type_name or "-"))
    if not args.no_services:
        print("  service:")
        for service in graph.services():
            print("    %-24s [%s]" % (service.name, service.type))
        for action in graph.actions():
            print("    %-24s [%s] (action)" % (action.name, action.type))
    print("  cmd    : publish geometry_msgs/msg/Twist on %s, or use the console" % TOPIC_CMD_VEL)
    sys.stdout.flush()

    stop_event = threading.Event()
    threads: list[threading.Thread] = []
    if goal is not None:
        thread = threading.Thread(target=run_goal_driver, args=(car, graph, goal, stop_event), name="goal", daemon=True)
        thread.start()
        threads.append(thread)

    interactive = sys.stdin is not None and sys.stdin.isatty() and not args.no_console
    if interactive:
        thread = threading.Thread(
            target=console_loop, args=(car, world, graph, stop_event), name="console", daemon=True
        )
        thread.start()
        threads.append(thread)
    elif not args.no_console:
        LOG.info("stdin is not a tty, interactive console disabled")

    try:
        if args.duration > 0:
            stop_event.wait(args.duration)
        else:
            while not stop_event.is_set():
                stop_event.wait(0.5)
    except KeyboardInterrupt:
        print()
        LOG.info("interrupted")
    finally:
        stop_event.set()
        car.stop()
        for thread in threads:
            thread.join(timeout=1.0)
        stop_server(server)

    x, y, theta = car.pose
    print("stopped after %.2fs at (%.3f, %.3f, %.3f rad)" % (car.sim_time, x, y, theta))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())