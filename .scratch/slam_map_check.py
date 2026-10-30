# -*- coding: utf-8 -*-
"""Verify the three map files the live-SLAM run wrote."""
import binascii
import io
import os
import struct
import sys
import zlib

sys.stdout.reconfigure(encoding="utf-8")
SC = r"E:\DesktopData\app\code\CPU\.scratch"
ok = bad = 0


def check(name, cond, detail=""):
    global ok, bad
    if cond:
        ok += 1
        print("  PASS  %-52s %s" % (name, detail))
    else:
        bad += 1
        print("  FAIL  %-52s %s" % (name, detail))


def read_pgm(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    magic, dims, maxv = parts[0], parts[1], parts[2]
    w, h = [int(x) for x in dims.split()]
    return magic, w, h, int(maxv), parts[3]


def decode_png(path):
    d = open(path, "rb").read()
    check("png signature", d[:8] == b"\x89PNG\r\n\x1a\n")
    pos, chunks = 8, []
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        crc = d[pos + 8 + ln:pos + 12 + ln]
        good = binascii.crc32(typ + body) & 0xFFFFFFFF == struct.unpack(">I", crc)[0]
        chunks.append((typ.decode("ascii"), body, good))
        check("crc %s" % typ.decode("ascii"), good)
        pos += 12 + ln
    ihdr = chunks[0][1]
    w, h, bd, ct, comp, filt, inter = struct.unpack(">IIBBBBB", ihdr)
    check("IHDR bit depth == 8 (8-bit PNG)", bd == 8, "bitdepth=%d" % bd)
    check("IHDR colour type is grey/alpha", ct in (0, 4), "colourtype=%d" % ct)
    check("IHDR no interlace", inter == 0)
    raw = b"".join(b for t, b, g in chunks if t == "IDAT")
    data = zlib.decompress(raw)
    bpp = 1 if ct == 0 else 2
    stride = w * bpp
    check("IDAT inflates to (stride+1)*h", len(data) == (stride + 1) * h,
          "%d == %d" % (len(data), (stride + 1) * h))
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    filters = set()
    for _ in range(h):
        f = data[p]
        filters.add(f)
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
    check("PNG filters used are all legal 0-4", filters <= set(range(5)), "%s" % sorted(filters))
    return w, h, ct, bytes(out)


print("=== files on disk ===")
for n in ["slam_live6.pgm", "slam_live6_pgm.pgm", "slam_live6_pgm.yaml",
          "slam_live6_png.png", "slam_live6_png.yaml"]:
    p = os.path.join(SC, n)
    check("exists %s" % n, os.path.exists(p),
          "%d B" % os.path.getsize(p) if os.path.exists(p) else "MISSING")

print("=== map_dump.exe PGM ===")
dm, dw, dh, dmx, dpx = read_pgm(os.path.join(SC, "slam_live6.pgm"))
check("map_dump magic P5", dm == b"P5", dm.decode())
check("map_dump dims 473x473", (dw, dh) == (473, 473), "%dx%d" % (dw, dh))
check("map_dump maxval 255", dmx == 255)
check("map_dump payload len", len(dpx) == dw * dh)
dump_hist = {v: dpx.count(v) for v in (205, 254)}
print("     map_dump greys: unknown(205)=%d free(254)=%d other=%d"
      % (dump_hist[205], dump_hist[254], dw * dh - dump_hist[205] - dump_hist[254]))

print("=== map_saver_cli.exe PGM ===")
sm, sw, sh, smx, spx = read_pgm(os.path.join(SC, "slam_live6_pgm.pgm"))
check("map_saver magic P5", sm == b"P5", sm.decode())
check("map_saver dims match map_dump", (sw, sh) == (dw, dh), "%dx%d" % (sw, sh))
check("map_saver payload len", len(spx) == sw * sh)
hist = {v: spx.count(v) for v in (0, 205, 254)}
print("     map_saver greys: occupied(0)=%d free(254)=%d unknown(205)=%d"
      % (hist[0], hist[254], hist[205]))
check("map_saver PGM has occupied cells", hist[0] > 0)
check("map_saver PGM has free cells", hist[254] > 0)
nonzero_diff = sum(1 for a, b in zip(dpx, spx) if a != b)
print("     pixels differing from map_dump's PGM: %d / %d" % (nonzero_diff, dw * dh))

print("=== map_saver_cli.exe PNG ===")
pw, ph, pct, ppx = decode_png(os.path.join(SC, "slam_live6_png.png"))
check("png dims match the PGM", (pw, ph) == (sw, sh), "%dx%d" % (pw, ph))
if pct == 0:
    same = sum(1 for a, b in zip(ppx, spx) if a != b)
    check("png pixels == pgm pixels", same == 0, "%d differing" % same)
else:
    grey = bytes(ppx[i] for i in range(0, len(ppx), 2))
    alpha = set(ppx[i] for i in range(1, len(ppx), 2))
    same = sum(1 for a, b in zip(grey, spx) if a != b)
    check("png grey channel == pgm pixels", same == 0, "%d differing" % same)
    check("png alpha is constant", len(alpha) == 1, "alpha=%s" % sorted(alpha)[:3])

print("=== yaml metadata ===")
for n in ["slam_live6_pgm.yaml", "slam_live6_png.yaml"]:
    print("  --- %s" % n)
    print("  " + io.open(os.path.join(SC, n), encoding="utf-8").read().replace("\n", "\n  ").rstrip())

print("=== independent readers ===")
try:
    from PIL import Image
    for n in ["slam_live6_png.png", "slam_live6_pgm.pgm", "slam_live6.pgm"]:
        im = Image.open(os.path.join(SC, n))
        im.load()
        check("Pillow opens %s" % n, True, "%s %s %s" % (im.format, im.mode, im.size))
except ImportError as exc:
    print("  SKIP  Pillow not installed:", exc)

print("\n%d passed, %d failed" % (ok, bad))
sys.exit(1 if bad else 0)