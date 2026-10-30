#!/usr/bin/env python3
"""Create ROS2-style .h aliases for shim headers that only ship as .hpp.

Upstream ROS2 is inconsistent about extensions: tf2, tf2_ros, message_filters
and angles publish `.h` headers, while tf2_geometry_msgs / tf2_sensor_msgs
publish `.hpp`.  The shim standardised on `.hpp`; ported packages that follow
upstream (slam_toolbox, for example) still `#include "tf2_ros/buffer.h"`.

This tool writes a one-line forwarding header next to every `.hpp` in the
packages that are `.h` upstream, so both spellings resolve.
"""
import os
import sys
from pathlib import Path

# This used to be one checkout on this desk, spelled out; from any other
# clone (and on Linux, where the path does not exist at all) the walk below
# found no packages and the tool "succeeded" without writing anything. The
# tree is located from the file itself instead: tools/ -> ros2_shim/.
SHIM = str(Path(__file__).resolve().parent.parent / "include")
H_STYLE_PACKAGES = ["tf2", "tf2_ros", "message_filters", "angles"]

TEMPLATE = """// Auto-generated alias so that upstream-style `#include "{hdr}"` resolves.
// The shim ships this header as `{hpp_name}`; see tools/make_ros_header_aliases.py.
#include "{hpp_name}"
"""

MARKER = "Auto-generated alias so that upstream-style"


def main():
    written = 0
    skipped = 0
    for pkg in H_STYLE_PACKAGES:
        root = os.path.join(SHIM, pkg)
        if not os.path.isdir(root):
            print("skip missing package: %s" % pkg)
            continue
        for r, d, fs in os.walk(root):
            for f in sorted(fs):
                if not f.endswith(".hpp"):
                    continue
                hpp_path = os.path.join(r, f)
                h_path = hpp_path[:-4] + ".h"
                rel = os.path.relpath(h_path, SHIM).replace(os.sep, "/")
                if os.path.exists(h_path):
                    existing = open(h_path, encoding="utf-8", errors="ignore").read()
                    if MARKER in existing:
                        skipped += 1
                        continue
                    print("HAND-WRITTEN, leaving alone: %s" % rel)
                    continue
                content = TEMPLATE.format(hdr=os.path.relpath(
                    hpp_path, SHIM).replace(os.sep, "/"), hpp_name=f)
                with open(h_path, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(content)
                written += 1
    print("wrote %d alias headers, %d already present" % (written, skipped))


if __name__ == "__main__":
    main()
