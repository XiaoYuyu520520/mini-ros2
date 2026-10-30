import io, sys
sys.stdout.reconfigure(encoding="utf-8")
for p, needles in [(r"bridge\docs\03-validation.md", ["## \u8bc1\u636e 50", "### \u4e5d\u3001\u8de8\u673a\u8865\u6d4b"]),
                   (r"bridge\ubuntu\ros2_bridge_sim\messages.py", ["RCL_LOG =", "    RCL_LOG: ["],),
                   (r"bridge\ubuntu\ros2_bridge_sim\tests\test_cdr_matches_rosbags.py", ["def sample_log", "    sample_log,"]),
                   (r"bridge\docs\04-gaps.md", ["### \u5341\u3001"]),
                   (r"bridge\docs\02-running.md", ["### \u770b\u65e5\u5fd7", "NAV2_SHM_ROSOUT |"])]:
    lines = io.open(p, encoding="utf-8-sig", newline="").read().split(chr(10))
    for i, l in enumerate(lines):
        for n in needles:
            if l.startswith(n) or n in l[:40]:
                print(p, "->", i+1, l[:60]); break
