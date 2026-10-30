# -*- coding: utf-8 -*-
"""Spot-check the src/racecar tree edits: XML/launch still parse, the launch defaults resolve,
every directory CMakeLists installs exists, and the map yamls name a file that is really there.
"""
import io
import os
import re
import xml.etree.ElementTree as ET

R = r"code\Desktop\racecar\src\racecar"
ok = True

# --- package.xml: parses, and every dep the scripts need is declared -----------
root = ET.parse(os.path.join(R, "package.xml")).getroot()
deps = {}
for child in root:
    if child.tag.endswith("depend"):
        deps.setdefault(child.tag, []).append(child.text.strip())
allnames = {n for v in deps.values() for n in v}
for tag in sorted(deps):
    print("  %-14s %s" % (tag, " ".join(sorted(deps[tag]))))
for need in ("nav2_msgs", "std_srvs", "nav2_simple_commander", "rclpy", "nav2_bringup",
             "nav2_common", "nav2_waypoint_cycle", "launch", "launch_ros"):
    good = need in allnames
    ok &= good
    print("  %-7s %s" % ("ok " if good else "MISSING", need))

# --- launch files: still valid Python ----------------------------------------
for name in ("Run_nav.launch.py", "bringup_launch.py"):
    p = os.path.join(R, "launch", name)
    t = io.open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
    try:
        compile(t, p, "exec")
        print("compiles: %s" % name)
    except SyntaxError as e:
        ok = False
        print("SYNTAX FAIL %s: %s line %s" % (name, e.msg, e.lineno))

# --- launch defaults: no car-only absolute paths, no live dead list -----------
t = io.open(os.path.join(R, "launch", "Run_nav.launch.py"), encoding="utf-8").read().replace("\r\n", "\n")
for probe, expect in (("'/root/Desktop/racecar", False),
                      ("'/home/davinci-mini", False),
                      ("default=os.path.join(racecar_dir, 'map', 'ai_map.yaml')", True),
                      ("param_dir, 'nav.yaml'", True)):
    good = (probe in t) == expect
    ok &= good
    print("  %-7s Run_nav.launch.py %r present=%s (want %s)" % ("ok " if good else "BAD", probe, probe in t, expect))

t2 = io.open(os.path.join(R, "launch", "bringup_launch.py"), encoding="utf-8").read()
live = [l for l in t2.splitlines() if re.match(r"^\s*lifecycle_nodes\s*=", l)]
ok &= not live
print("  %-7s bringup_launch.py live `lifecycle_nodes` assignments: %d" % ("ok " if not live else "BAD", len(live)))

# --- what CMakeLists installs has to exist in the source tree -----------------
cm = io.open(os.path.join(R, "CMakeLists.txt"), encoding="utf-8").read()
dirs = re.findall(r"install\(DIRECTORY\s+(.*?)DESTINATION share/\$\{PROJECT_NAME\}", cm, re.S)
installed_dirs = [d for group in dirs for d in group.split()]
scripts = re.findall(r'"?scripts/([^"\n]+?)"?\n', cm)
print("  install(DIRECTORY ...): %s" % ", ".join(installed_dirs))
print("  install(PROGRAMS/FILES): %d entries" % len(scripts))
for name in installed_dirs:
    good = os.path.isdir(os.path.join(R, name))
    ok &= good
    print("  %-7s src/racecar/%s/" % ("ok " if good else "MISSING", name))
for name in scripts:
    good = os.path.exists(os.path.join(R, "scripts", name))
    ok &= good
    if not good:
        print("  %-7s src/racecar/scripts/%s" % ("MISSING", name))
print("  ok      all %d script/csv entries exist" % len(scripts) if all(
    os.path.exists(os.path.join(R, "scripts", n)) for n in scripts) else "")

# --- the file Run_nav defaults to has to be one CMakeLists installs -----------
good = "map" in installed_dirs and os.path.exists(os.path.join(R, "map", "ai_map.yaml"))
ok &= good
print("  %-7s Run_nav's default map ships: src/racecar/map/ai_map.yaml" % ("ok " if good else "BAD"))

# --- map yamls: every `image:` has to resolve next to its own yaml ------------
for p in [os.path.join(R, "maps", "ceshi1.yaml"), os.path.join(R, "map", "ai_map.yaml"),
          os.path.join(R, "map", "test_map.yaml")]:
    txt = io.open(p, encoding="utf-8").read()
    m = re.search(r"^image:\s*(\S+)\s*$", txt, re.M)
    if not m:
        ok = False
        print("  MISSING image: in %s" % p)
        continue
    image = m.group(1)
    resolved = image if os.path.isabs(image) else os.path.join(os.path.dirname(p), image)
    good = os.path.exists(resolved)
    ok &= good
    print("  %-7s %-28s image=%-14s -> %s" % ("ok " if good else "MISSING",
                                              os.path.basename(p), image, os.path.normpath(resolved)))

print()
print("VERDICT:", "all checks pass" if ok else "SOMETHING FAILED")
