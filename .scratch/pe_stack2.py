# -*- coding: utf-8 -*-
"""Part two of the `/parameter_events` stack check, against a stack already up.

Two different server processes set one parameter each while a third process
reads the stream; then the stack is stopped. The stack reloads its parameters
from the YAML on the next start, so the values left behind do not outlive it.
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

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
CANDIDATES = ("bond_heartbeat_period", "costmap_update_timeout", "controller_frequency")


def say(text):
    print("\n=== %s" % text, flush=True)


def run(args, timeout=120):
    done = subprocess.run(["cmd", "/c", *[str(a) for a in args]], cwd=ROOT,
                          capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)
    out = (done.stdout or "") + (done.stderr or "")
    print(out.rstrip(), flush=True)
    return done, out


def ros2(*args, timeout=120):
    return run([ROS2_BAT, *args], timeout=timeout)


def main():
    done, nodes = ros2("node", "list")
    if "/controller_server" not in nodes or "/planner_server" not in nodes:
        say("the stack is not up; start it with nav2_stack.bat first")
        return 1

    say("ros2 topic info /parameter_events (the whole stack, before)")
    ros2("topic", "info", "/parameter_events")

    chosen = []
    for node in ("/controller_server", "/planner_server"):
        for name in CANDIDATES:
            done, out = ros2("param", "get", node, name, timeout=60)
            match = re.search(r"Double value is:\s*([-\d.eE+]+)", out)
            if match:
                chosen.append((node, name, float(match.group(1))))
                break
    say("parameters to move: %s" % chosen)
    if len(chosen) != 2:
        return 1

    say("starting parameter_events_probe.exe in its own process")
    probe_log = io.open(PROBE_LOG, "w", encoding="utf-8", errors="replace")
    probe = subprocess.Popen(["cmd", "/c", PROBE_BAT, "--count", "2", "--timeout", "40"],
                             cwd=ROOT, stdout=probe_log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    time.sleep(2.0)

    for node, name, value in chosen:
        say("ros2 param set %s %s %g  (was %g)" % (node, name, value + 1.0, value))
        ros2("param", "set", node, name, "%g" % (value + 1.0), timeout=60)
        time.sleep(0.5)

    probe.wait(timeout=90)
    probe_log.close()
    say("parameter_events_probe.exe output")
    print(io.open(PROBE_LOG, encoding="utf-8", errors="replace").read().rstrip(), flush=True)

    say("ros2 topic info /parameter_events (the whole stack, after)")
    ros2("topic", "info", "/parameter_events")

    say("nav2_stack.bat stop")
    run([STACK_BAT, "stop"], timeout=180)
    return 0


if __name__ == "__main__":
    sys.exit(main())