# -*- coding: utf-8 -*-
"""Load every mission script under the shim, without running its ``main()``.

Two things are being asked. First that the module-level imports still resolve
on the shim (the message aliases, the vendored ``nav2_simple_commander``,
``numpy``) - ``runpy.run_path`` with a name other than ``__main__`` runs exactly
that much and no more. Second, for every script that has one, what
``waypoint_file()`` answers: with no argument it has to land on a file that
exists next to the script, and with ``sim_room12.csv`` given it has to land on
that one. Before this the scripts said ``test.csv``, which is not in the
repository, so the answer was a ``FileNotFoundError``.
"""
import io
import os
import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CPU = HERE.parent
SHIM = CPU / "nav2" / "ros2_shim" / "python"
SCRIPTS = CPU / "code" / "Desktop" / "racecar" / "src" / "racecar" / "scripts"

sys.path.insert(0, str(SHIM))
import shim_run  # noqa: E402 - the path has to be right first
shim_run.install_paths()

names = sorted(p.name for p in SCRIPTS.iterdir() if p.suffix == ".py")
loaded, failures, rows = 0, [], []
for name in names:
    path = SCRIPTS / name
    try:
        module = runpy.run_path(str(path), run_name="probe")
    except Exception as error:  # noqa: BLE001 - the point is to list them all
        failures.append((name, "%s: %s" % (type(error).__name__, error)))
        continue
    loaded += 1
    fn = module.get("waypoint_file")
    if fn is None:
        rows.append((name, None, None, None))
        continue
    sys.argv = [str(path)]
    default = fn()
    sys.argv = [str(path), "sim_room12.csv"]
    named = fn()
    sys.argv = [str(path), str(SCRIPTS / "ai_test1.csv")]
    absolute = fn()
    rows.append((name, default, named, absolute))

out = io.StringIO()
out.write("loaded %d/%d scripts under the shim\n" % (loaded, len(names)))
for name, why in failures:
    out.write("  IMPORT FAILED %s: %s\n" % (name, why))
out.write("\n%-32s %-18s %-16s %s\n" % ("script", "default", "argv csv", "abs csv"))
for name, default, named, absolute in rows:
    if default is None:
        out.write("%-32s %s\n" % (name, "(no waypoint_file; reads a fixed csv)"))
        continue
    assert os.path.exists(default), (name, default)
    assert os.path.basename(named) == "sim_room12.csv", (name, named)
    assert absolute == str(SCRIPTS / "ai_test1.csv"), (name, absolute)
    out.write("%-32s %-18s %-16s %s\n"
              % (name, os.path.basename(default), os.path.basename(named), os.path.basename(absolute)))
out.write("\nall waypoint_file() answers exist, and the named one is the one that was named\n")
text = out.getvalue()
print(text)
io.open(HERE / "waypoint_file.out", "w", encoding="utf-8", newline="\n").write(text)
