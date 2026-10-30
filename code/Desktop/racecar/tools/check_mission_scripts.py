#!/usr/bin/env python3
"""Every mission script has to load under the shim, and point at real files.

``src/racecar/CMakeLists.txt`` installs the seventeen scripts and the four
route CSVs, and the car runs them as ``ros2 run racecar <script> <route>.csv``.
Both halves of that are easy to get wrong in ways nothing else notices: a script
can stop importing (the shim's rclpy or its message names move, an import is
misspelled), ``waypoint_file()`` can resolve to a route that is not on disk, and
a route file can be added to ``scripts/`` without ever being installed - the
script then works in this checkout and not on the car.

The check runs *through the shim* rather than by plain ``python``, because plain
python cannot import ``geometry_msgs`` or ``nav2_msgs`` at all: there is no ROS2
on the Windows side of this repository (BUILD.md), so an import test that does
not go through ``nav2/ros2_shim/python/shim_run.py`` tests nothing.

    python code/Desktop/racecar/tools/check_mission_scripts.py

The parent run re-executes itself as ``shim_run.py --path src/racecar/scripts
<this file> --driver <scripts...>``, so the shim's import path and its message
aliases are in place before the first mission script is imported. Called with
``--driver`` (what that subprocess does) it imports each script as a module -
``__name__`` is ``_mission_check_N``, never ``__main__``, so no mission starts -
and checks, per script:

* it imports, and the ``rclpy`` that loaded is the shim's
* ``waypoint_file()`` answers with a file that exists, for its own default
  route, for a route named on the command line, and for an absolute one
* every ``*.csv`` it names exists, and it is one of the installed programs

and, once for the tree, that every route CSV is four numeric columns whose last
two are a unit quaternion - the third column is ``quaternion.z`` and not an
altitude (``bridge/docs/04-gaps.md`` section 28 item 12, measured in
``bridge/docs/03-validation.md`` evidence 80) - and that no script still reads
it as one.

Exit code 0 when all of that holds, 1 otherwise.
"""
from __future__ import annotations

import ast
import csv
import importlib.util
import io
import math
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RACECAR = HERE.parent
REPO_ROOT = HERE.parents[3]
SCRIPTS = RACECAR / "src" / "racecar" / "scripts"
CMAKE = RACECAR / "src" / "racecar" / "CMakeLists.txt"
SHIM_RUN = REPO_ROOT / "nav2" / "ros2_shim" / "python" / "shim_run.py"

# The two command-line probes are deliberately different files. A resolver that
# echoed its own default back would pass the first check on its own.
RELATIVE_PROBE = "sim_room12.csv"
ABSOLUTE_PROBE = "ai_test1.csv"

# ``position.z = point["z"]`` reads the route's third column as an altitude. It
# is ``quaternion.z``; the heading bug that came of it is measured in evidence
# 80 - a goal whose heading was 115 degrees off, arrived at 170 degrees off.
ORIENTATION_BUG = re.compile(r"""\.position\.z\s*=\s*[A-Za-z_]\w*\s*\[\s*['"]z['"]\s*\]""")

# Any ``*.csv`` a script names is one it has to read, except the ones it writes:
# a recorded lap is called ``out_*.csv`` (``get.py`` writes ``out_test.csv``).
PRODUCED_PREFIX = "out_"
CSV_LITERAL = re.compile(r"""['"]([^'"]*\.[Cc][Ss][Vv])['"]""")

# ``|q| == 1`` to within float noise in the file, not to within what a controller
# would tolerate: this is a shape check on the route, not on the robot.
QUATERNION_TOLERANCE = 1e-3


def cmake_installs(keyword: str, suffix: str) -> list:
    """The file names one ``install(<keyword> ...)`` block installs."""
    text = io.open(CMAKE, encoding="utf-8", errors="replace").read()
    found = []
    for block in re.findall(r"install\(%s(.*?)\)" % keyword, text, re.S):
        # `"scripts/go copy.py"` is one file name and not two, so the split has
        # to know about the quotes CMake needs for the names with spaces in them.
        for token in re.findall(r'"[^"]*"|\S+', block):
            token = token.strip('"')
            if token.lower().endswith(suffix):
                found.append(token.replace("\\", "/").split("/")[-1])
    return found


def load(path: Path, index: int):
    """Import a mission script as a module, so its ``__main__`` guard stays shut."""
    name = "_mission_check_%d" % index
    spec = importlib.util.spec_from_file_location(name, str(path))
    if spec is None or spec.loader is None:
        raise ImportError("no import spec for %s" % path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def waypoint_answer(module, path: Path, extra) -> str:
    """What ``waypoint_file()`` says, with ``argv`` set the way a run would."""
    saved = sys.argv
    sys.argv = [str(path)] + list(extra)
    try:
        return str(module.waypoint_file())
    finally:
        sys.argv = saved


def check_script(path: Path, index: int) -> tuple:
    """The table row for one script, and everything wrong with it."""
    row = {"name": path.name, "default": "-", "relative": "-", "absolute": "-"}
    problems = []
    notes = []
    source = io.open(path, encoding="utf-8", errors="replace").read()

    match = ORIENTATION_BUG.search(source)
    if match:
        line = source[: match.start()].count("\n") + 1
        problems.append("%s:%d reads the route's third column as an altitude "
                        "(it is quaternion.z): %s"
                        % (path.name, line, match.group(0)))

    try:
        module = load(path, index)
    except Exception as error:  # noqa: BLE001 - an import fails any way it likes
        row["default"] = "IMPORT FAILED"
        problems.append("%s: import failed: %s: %s"
                        % (path.name, type(error).__name__, error))
        return row, problems, notes

    if getattr(module, "waypoint_file", None) is None:
        for label in ("default", "relative", "absolute"):
            row[label] = "(no waypoint_file; reads a fixed csv)"
    else:
        probes = (("default", []),
                  ("relative", [RELATIVE_PROBE]),
                  ("absolute", [str(SCRIPTS / ABSOLUTE_PROBE)]))
        for label, extra in probes:
            try:
                answer = waypoint_answer(module, path, extra)
            except Exception as error:  # noqa: BLE001
                row[label] = "RAISED %s" % type(error).__name__
                problems.append("%s: waypoint_file(%s) raised %s: %s"
                                % (path.name, extra, type(error).__name__, error))
                continue
            row[label] = Path(answer).name
            if not os.path.isfile(answer):
                problems.append("%s: the %s route does not exist: %s"
                                % (path.name, label, answer))
            if label == "relative" and Path(answer).parent != SCRIPTS:
                problems.append("%s: a route named on the command line did not "
                                "resolve next to the script: %s" % (path.name, answer))
            if label == "absolute" and answer != str(SCRIPTS / ABSOLUTE_PROBE):
                problems.append("%s: an absolute route came back as %s"
                                % (path.name, answer))

    # A route the script names but that is not there is a *note* here, because
    # `waypoint_file()` has already answered with a file that exists: `test.csv`
    # is the car's own route, looked for first and falling through to the
    # packaged `ai_test.csv` when the car has not been given one, so a missing
    # `test.csv` in this checkout is the design and not a typo. It is a problem
    # for a script that reads a fixed csv instead, where nothing probed the
    # answer and a missing name is a route that cannot be found at all.
    probed = row["default"] != "(no waypoint_file; reads a fixed csv)"
    for name in sorted(set(CSV_LITERAL.findall(source))):
        base = name.replace("\\", "/").split("/")[-1]
        if not base[:-4] or "*" in base or base.startswith(PRODUCED_PREFIX):
            continue
        if (SCRIPTS / base).is_file():
            continue
        (notes if probed else problems).append(
            "%s names a route that is not in scripts/: %s" % (path.name, name))
    return row, problems, notes


def check_routes() -> tuple:
    """Every route CSV: four numeric columns, unit quaternion in the last two."""
    routes = sorted(SCRIPTS.glob("*.csv"))
    problems = []
    installed = cmake_installs("FILES", ".csv")
    for route in routes:
        if route.name not in installed:
            problems.append("%s is not installed by CMakeLists.txt" % route.name)
        text = io.open(route, encoding="utf-8", errors="replace", newline="").read()
        rows = [row for row in csv.reader(io.StringIO(text))
                if [cell for cell in row if cell.strip()]]
        if not rows:
            problems.append("%s is empty" % route.name)
        for number, row in enumerate(rows, 1):
            cells = [cell.strip() for cell in row if cell.strip()]
            if len(cells) != 4:
                problems.append("%s:%d has %d columns, expected "
                                "x, y, quaternion.z, quaternion.w"
                                % (route.name, number, len(cells)))
                continue
            try:
                values = [float(cell) for cell in cells]
            except ValueError:
                problems.append("%s:%d is not four numbers: %s"
                                % (route.name, number, row))
                continue
            if all(value == 0.0 for value in values):
                continue  # a padding row, and ai_test2.csv ends with one
            norm = math.hypot(values[2], values[3])
            if abs(norm - 1.0) > QUATERNION_TOLERANCE:
                problems.append("%s:%d is not a unit quaternion in columns 3-4: "
                                "|q| = %.6f" % (route.name, number, norm))
    return routes, problems


def driver(argv: list) -> int:
    """The half that runs under shim_run, with the shim's import path installed."""
    try:
        import rclpy
    except Exception as error:  # noqa: BLE001
        print("check_mission_scripts: cannot import rclpy: %s" % error)
        return 1
    origin = (getattr(rclpy, "__file__", "") or "").replace("\\", "/")
    if "ros2_shim" not in origin:
        # Without this the check would be vacuously green on any machine that
        # happens to have a real ROS2 installed, which is not what it claims.
        print("check_mission_scripts: that rclpy is not the shim's: %s" % origin)
        return 1
    print("rclpy  %s" % origin)

    programs = cmake_installs("PROGRAMS", ".py")
    rows = []
    problems = []
    notes = []
    loaded = 0
    for index, name in enumerate(argv):
        path = Path(name)
        row, found, found_notes = check_script(path, index)
        if row["default"] != "IMPORT FAILED":
            loaded += 1
        if path.name not in programs:
            found.append("%s is in scripts/ but is not one of the programs "
                         "CMakeLists.txt installs" % path.name)
        row["installed"] = "yes" if path.name in programs else "MISSING"
        rows.append(row)
        problems.extend(found)
        notes.extend(found_notes)

    routes, route_problems = check_routes()
    problems.extend(route_problems)
    missing = [name for name in programs
               if not (SCRIPTS / name).is_file()]

    print("loaded %d/%d scripts under the shim" % (loaded, len(argv)))
    print()
    print("%-34s %-18s %-18s %-18s %s"
          % ("script", "default", "argv csv", "abs csv", "installed"))
    for row in rows:
        print("%-34s %-18s %-18s %-18s %s"
              % (row["name"][:34], row["default"][:18], row["relative"][:18],
                 row["absolute"][:18], row["installed"]))
    print()
    print("routes  %s" % ", ".join(route.name for route in routes))
    for name in missing:
        problems.append("CMakeLists.txt installs %s, which is not in scripts/" % name)
    for note in notes:
        print("NOTE     %s" % note)

    if problems:
        print()
        for problem in problems:
            print("PROBLEM  %s" % problem)
        print()
        print("%d problem(s)" % len(problems))
        return 1
    print("all %d scripts load under the shim, every waypoint_file() answer "
          "exists," % loaded)
    print("every route in scripts/ is installed, and no route is read as an "
          "altitude")
    return 0


def parent(argv: list) -> int:
    scripts = sorted(SCRIPTS.glob("*.py"))
    if not scripts:
        print("check_mission_scripts: no scripts under %s" % SCRIPTS)
        return 1
    if not SHIM_RUN.is_file():
        print("check_mission_scripts: no shim runner at %s" % SHIM_RUN)
        return 1
    command = [sys.executable, str(SHIM_RUN), "--path", str(SCRIPTS),
               str(Path(__file__).resolve()), "--driver"]
    command.extend(str(path) for path in scripts)
    environment = dict(os.environ)
    environment["PYTHONUTF8"] = "1"
    environment["PYTHONIOENCODING"] = "utf-8"
    done = subprocess.run(command, capture_output=True, env=environment)
    sys.stdout.write(done.stdout.decode("utf-8", "replace"))
    sys.stderr.write(done.stderr.decode("utf-8", "replace"))
    return done.returncode


def main(argv: list) -> int:
    if argv and argv[0] == "--driver":
        return driver(argv[1:])
    if argv and argv[0] in ("-h", "--help"):
        sys.stdout.write(__doc__)
        return 0
    if argv:
        sys.stderr.write("usage: check_mission_scripts.py\n")
        return 2
    return parent(argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
