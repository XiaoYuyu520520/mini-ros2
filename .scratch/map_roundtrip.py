# -*- coding: utf-8 -*-
"""Round-trip a saved map through map_server and back.

The live SLAM run showed that map_dump.exe's PGM was the *vertical mirror* of
map_saver_cli.exe's for the same grid (48262 of 223729 pixels differed, and
flipping one of them made the difference exactly 0). map_dump wrote the grid in
its own row order; a ROS map file's first row is the highest y.

So: with the fix in place, hand each saved map to map_server, let it publish
/map, read that back with map_dump and compare against the file we started
from. If the orientation is right the bytes come back identical - and the PNG
half of it also proves map_server can read the PNG coder from step 4.
"""
import io
import os
import struct
import sys
import time
import zlib

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
BUILD = os.path.join(ROOT, "nav2", "ros2_shim", "build")
SC = os.path.join(ROOT, ".scratch")
WRAP = os.path.join(SC, "run_shim_exe.bat")
PARAMS = r"nav2\ros2_shim\config\nav2_map_server.yaml"
LOGDIR = os.path.join(SC, "map_roundtrip")
os.makedirs(LOGDIR, exist_ok=True)

LOG = []


def say(text):
    print(text, flush=True)
    LOG.append(text)


def run(args, t=120):
    import subprocess
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()


def bg(args, log):
    import subprocess
    fh = io.open(os.path.join(LOGDIR, log), "w", encoding="utf-8", errors="replace")
    return subprocess.Popen(["cmd", "/c", *args], cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT)


def logtext(name):
    return io.open(os.path.join(LOGDIR, name), encoding="utf-8", errors="replace").read()


def read_pgm(path):
    d = io.open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = [int(x) for x in parts[1].split()]
    return w, h, parts[3]


def png_pixels(path):
    d = io.open(path, "rb").read()
    pos, idat, ihdr = 8, b"", None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            ihdr = body
        elif typ == b"IDAT":
            idat += body
        pos += 12 + ln
    w, h, bd, ct = struct.unpack(">IIBB", ihdr[:10])
    bpp = 1 if ct == 0 else 2
    stride = w * bpp
    data = zlib.decompress(idat)
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        f = data[p]
        line = bytearray(data[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            x = line[i]
            if f == 1:
                x = (x + a) & 0xFF
            elif f == 2:
                x = (x + b) & 0xFF
            elif f == 3:
                x = (x + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                x = (x + pr) & 0xFF
            line[i] = x
        out += line
        prev = line
    return w, h, bytes(out[i * bpp] for i in range(len(out) // bpp))


def kill(name):
    import psutil
    n = 0
    for p in psutil.process_iter(["pid", "name"]):
        if (p.info["name"] or "").lower() == name:
            p.kill()
            n += 1
    return n


say("=== 0. clear leftover map nodes ===")
for name in ("map_server.exe", "map_saver.exe", "lifecycle_manager.exe", "map_dump.exe"):
    killed = kill(name)
    if killed:
        say("  killed %d x %s" % (killed, name))
time.sleep(1)

say("=== 1. bridge up, but no /map mirror: map_server is the map source ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"]))
time.sleep(2)
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start", "--mirror", "/clock",
         "--mirror", "/odom", "--mirror", "/scan", "--mirror", "/tf",
         "--mirror", "/tf_static"], t=240))

CASES = [("pgm", os.path.join(SC, "slam_live6_pgm.yaml")),
         ("png", os.path.join(SC, "slam_live6_png.yaml"))]
results = {}

for tag, yaml_path in CASES:
    say("=== 2.%s map_server loads %s ===" % (tag, os.path.basename(yaml_path)))
    srv = bg([WRAP, os.path.join(BUILD, "map_server.exe"), "--params-file", PARAMS,
              "-p", "yaml_filename:=" + yaml_path.replace("\\", "/")],
             "map_server_%s.log" % tag)
    time.sleep(2)
    # The manager's node_names list is ["map_server", "map_saver"], so it waits
    # for both to answer before it reports "Managed nodes are active".
    sav = bg([WRAP, os.path.join(BUILD, "map_saver.exe"), "--params-file", PARAMS],
             "map_saver_%s.log" % tag)
    time.sleep(2)
    mgr = bg([WRAP, os.path.join(BUILD, "lifecycle_manager.exe"), "--params-file", PARAMS],
             "manager_%s.log" % tag)
    deadline = time.time() + 75
    active = False
    while time.time() < deadline:
        if "Managed nodes are active" in logtext("manager_%s.log" % tag):
            active = True
            break
        time.sleep(3)
    say("  lifecycle: %s" % ("ACTIVE" if active else "NOT ACTIVE"))
    if not active:
        say(logtext("manager_%s.log" % tag)[-800:])
        say(logtext("map_server_%s.log" % tag)[-800:])
    say("--- map_server log ---")
    say(logtext("map_server_%s.log" % tag)[-900:])

    out = os.path.join(SC, "rt_from_%s.pgm" % tag)
    say("--- map_dump.exe reading /map back ---")
    say(run([WRAP, os.path.join(BUILD, "map_dump.exe"), "--topic", "/map",
             "--settle", "5", "--ascii", "48", "--out", out], t=180))
    results[tag] = out

    mgr.kill()
    sav.kill()
    srv.kill()
    time.sleep(3)

say("=== 3. compare ===")
w1, h1, src_pgm = read_pgm(os.path.join(SC, "slam_live6_pgm.pgm"))
w2, h2, src_png = png_pixels(os.path.join(SC, "slam_live6_png.png"))
say("  source PGM  %dx%d  source PNG %dx%d" % (w1, h1, w2, h2))

diffs = {}
for tag, out in results.items():
    try:
        w, h, got = read_pgm(out)
    except Exception as exc:
        say("  %-4s could not read %s: %s" % (tag, out, exc))
        continue
    say("  %-4s round trip wrote %dx%d" % (tag, w, h))
    ref = src_pgm
    same = sum(1 for a, b in zip(got, ref) if a != b) if len(got) == len(ref) else -1
    diffs[tag] = same
    say("  %-4s differing from the source PGM: %s" % (tag, same if same >= 0 else "size mismatch"))

if len(results) == 2 and all(t in diffs for t in results):
    a = read_pgm(results["pgm"])[2]
    b = read_pgm(results["png"])[2]
    say("  pgm round trip vs png round trip: %d differing" % sum(1 for x, y in zip(a, b) if x != y))

say("=== 4. stop the daemon ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"]))

io.open(os.path.join(SC, "map_roundtrip.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\map_roundtrip.out")