import io, re, sys
sys.stdout.reconfigure(encoding="utf-8")
for f in ["clock.cpp", "uuid.cpp", "map_io.cpp", "test_nav2_ros_common.cpp"]:
    p = r"nav2\ros2_shim\tests" + "\\" + f
    src = io.open(p, encoding="utf-8-sig", newline="", errors="replace").read()
    print("=====", f, len(src), "bytes")
    lines = src.split("\n")
    # show helper definitions and exit-code plumbing
    for i, l in enumerate(lines, 1):
        if re.search(r"\b(expect|check|require|verify|fail|ok)\w*\s*\(|return [0-9]|int\s+\w+\s*\(", l):
            print("   %4d %s" % (i, l.rstrip()[:110]))