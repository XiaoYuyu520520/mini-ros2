# -*- coding: utf-8 -*-
"""Two *different processes* writing one `/parameter_events` ring.

Every nav2 server is its own executable, and each now publishes `/parameter_events`
into the same channel - which the shared-memory contract describes as "one writer
per channel". This starts the real stack, then sets one parameter on two
different servers while a third process reads the stream, so both the payload
and the multi-writer case are checked against the stack that actually runs.
"""
import io
import os
import re
import subprocess
import sys
import time

ROOT = r"E:\DesktopData\app\code\CPU"
SCRATCH = os.path.join(ROOT, ".scratch")
ROS2_BAT = os.path.join(ROOT, r"bridge\windows\ros2.bat")
STACK_BAT = os.path.join(ROOT, r"bridge\windows\nav2_stack.bat")
PROBE_BAT = os.path.join(SCRATCH, "pe_probe.bat")
PROBE_LOG = os.path.join(SCRATCH, "pe_probe_stack.out")
STACK_LOG = os.path.join(SCRATCH, "pe_stack.log")

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def say(text):
    print("\n=== %s" % text, flush=True)


def run(args, timeout=300):
    done = subprocess.run(["cmd", "/c", *[str(a) for a in args]], cwd=ROOT,
                          capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)
    out = (done.stdout or "") + (done.stderr or "")
    print(out.rstrip(), flush=True)
    return done, out


def ros2(*args, timeout=120):
    return run([ROS2_BAT, *args], timeout=timeout)


def double_parameter(node):
    """(name, value) of the first Double the node declares, or None."""
    done, listing = ros2("param", "list", node, timeout=60)
    for line in listing.splitlines():
        name = line.strip()
        if not name.startswith("  "):
            continue
        name = name.strip()
        if not name or name.startswith("/"):
            continue
        done, out = ros2("param", "get", node, name, timeout=60)
        match = re.search(r"Double value is:\s*([-\d.eE+]+)", out)
        if match:
            return name, float(match.group(1))
    return None


def main():
    say("nav2_stack.bat (the whole stack, on the six big cores)")
    log = io.open(STACK_LOG, "w", encoding="utf-8", errors="replace")
    stack = subprocess.Popen(["cmd", "/c", STACK_BAT], cwd=ROOT,
                             stdout=log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    stack.wait(timeout=600)
    log.close()
    tail = io.open(STACK_LOG, encoding="utf-8", errors="replace").read().splitlines()
    print("\n".join(tail[-12:]), flush=True)

    say("ros2 node list")
    ros2("node", "list")

    say("ros2 topic info /parameter_events (the whole stack publishing)")
    ros2("topic", "info", "/parameter_events")

    first = double_parameter("/controller_server")
    second = double_parameter("/planner_server")
    say("chosen parameters: controller_server %s, planner_server %s" % (first, second))
    if first is None or second is None:
        return 1

    say("starting parameter_events_probe.exe in its own process")
    probe_log = io.open(PROBE_LOG, "w", encoding="utf-8", errors="replace")
    probe = subprocess.Popen(["cmd", "/c", PROBE_BAT, "--count", "2", "--timeout", "40"],
                             cwd=ROOT, stdout=probe_log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    time.sleep(2.0)

    for node, pair in (("/controller_server", first), ("/planner_server", second)):
        name, value = pair
        say("ros2 param set %s %s %g" % (node, name, value + 1.0))
        ros2("param", "set", node, name, "%g" % (value + 1.0), timeout=60)
        time.sleep(0.5)

    probe.wait(timeout=90)
    probe_log.close()
    say("parameter_events_probe.exe output")
    print(io.open(PROBE_LOG, encoding="utf-8", errors="replace").read().rstrip(), flush=True)

    say("nav2_stack.bat stop")
    ros2("topic", "info", "/parameter_events")
    print()
    run([STACK_BAT, "stop"], timeout=180)
    return 0


if __name__ == "__main__":
    sys.exit(main())