# -*- coding: utf-8 -*-
"""Feed the shim's PNG reader files it did not write.

Pillow (libpng) writes the ordinary cases - L, LA, RGB, RGBA, palette, 16-bit -
and a hand-rolled encoder writes the four non-trivial row filters plus an
interlaced file and a file carrying chunks the reader has to skip. Everything
comes back through .scratch/png_read.exe, which prints the gray and alpha
buffers the shim's Magick++ decoded.

    python .scratch/png_interop.py
"""
import binascii
import os
import struct
import subprocess
import sys
import zlib

ROOT = r"E:\DesktopData\app\code\CPU"
OUT = os.path.join(ROOT, ".scratch", "pngin")
READ = os.path.join(ROOT, ".scratch", "png_read.exe")
SIG = b"\x89PNG\r\n\x1a\n"
failures = 0


def check(ok, what):
    global failures
    print("[%s] %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        failures += 1


def luma(r, g, b):
    """The formula include/Magick++.h documents: Rec. 601, rounded."""
    return (299 * r + 587 * g + 114 * b + 500) // 1000


def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF))


def filter_row(row, previous, filter_type, channels):
    stride = len(row)

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)

    out = bytearray(stride)
    for i in range(stride):
        left = row[i - channels] if i >= channels else 0
        up = previous[i]
        upleft = previous[i - channels] if i >= channels else 0
        if filter_type == 0:
            out[i] = row[i]
        elif filter_type == 1:
            out[i] = (row[i] - left) & 0xFF
        elif filter_type == 2:
            out[i] = (row[i] - up) & 0xFF
        elif filter_type == 3:
            out[i] = (row[i] - (left + up) // 2) & 0xFF
        else:
            out[i] = (row[i] - paeth(left, up, upleft)) & 0xFF
    return bytes(out)


def build(path, width, height, color_type, depth, rows, channels,
          filters=None, extra=(), interlace=0, bit_depth_in_header=None):
    payload = b""
    previous = bytes(width * channels)
    for index, row in enumerate(rows):
        kind = filters[index] if filters else 0
        payload += bytes([kind]) + filter_row(row, previous, kind, channels)
        previous = row
    header = struct.pack(
        ">IIBBBBB", width, height, bit_depth_in_header or depth, color_type, 0, 0, interlace)
    data = SIG + chunk(b"IHDR", header)
    for kind, body in extra:
        data += chunk(kind, body)
    data += chunk(b"IDAT", zlib.compress(payload, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)
    return path


def run(path):
    done = subprocess.run([READ, path], capture_output=True, text=True)
    out = done.stdout.strip().split("\n")
    if out[0].startswith("OK "):
        _, width, height, alpha = out[0].split()
        gray = bytes.fromhex(out[1])
        alpha_bytes = bytes.fromhex(out[2]) if int(alpha) else b""
        return (int(width), int(height), gray, alpha_bytes, out[0])
    return out[0]


def expect(path, what, gray=None, alpha=None, size=None, refused=None):
    got = run(path)
    if refused:
        ok = isinstance(got, str) and got.startswith("ERR ") and refused in got
        check(ok, "%s: refused by name (%s)" % (what, got if isinstance(got, str) else got))
        return
    if isinstance(got, str):
        check(False, "%s: %s" % (what, got))
        return
    width, height, got_gray, got_alpha, _ = got
    if size is not None:
        check((width, height) == size, "%s: %dx%d" % (what, width, height))
    if gray is not None:
        check(got_gray == bytes(gray), "%s: gray pixels match" % what)
    if alpha is not None:
        check(got_alpha == bytes(alpha),
              "%s: alpha pixels match (%s)" % (what, got_alpha.hex()))


os.makedirs(OUT, exist_ok=True)
for name in os.listdir(OUT):
    os.remove(os.path.join(OUT, name))

try:
    from PIL import Image
except ImportError:
    print("Pillow is required for this script")
    sys.exit(2)

# ---- 1. what Pillow writes ------------------------------------------------
GRAY_PIXELS = [0, 1, 100, 128, 205, 254, 255, 63, 200, 17, 240, 5]

image = Image.new("L", (4, 3))
image.putdata(GRAY_PIXELS)
image.save(os.path.join(OUT, "l.png"))
expect(os.path.join(OUT, "l.png"), "Pillow L (grayscale)",
       gray=GRAY_PIXELS, size=(4, 3), alpha=None)

image = Image.new("LA", (3, 2))
image.putdata([(0, 0), (128, 255), (255, 128), (64, 0), (200, 255), (1, 1)])
image.save(os.path.join(OUT, "la.png"))
expect(os.path.join(OUT, "la.png"), "Pillow LA (gray + alpha)",
       gray=[0, 128, 255, 64, 200, 1], alpha=[0, 255, 128, 0, 255, 1], size=(3, 2))

RGB_PIXELS = [(10, 200, 30), (255, 0, 0), (0, 255, 0), (0, 0, 255), (1, 2, 3), (200, 200, 200)]
image = Image.new("RGB", (3, 2))
image.putdata(RGB_PIXELS)
image.save(os.path.join(OUT, "rgb.png"))
expect(os.path.join(OUT, "rgb.png"), "Pillow RGB (converted through luma)",
       gray=[luma(*p) for p in RGB_PIXELS], size=(3, 2), alpha=None)

RGBA_PIXELS = [(10, 200, 30, 255), (255, 0, 0, 0), (0, 0, 255, 128), (7, 7, 7, 200)]
image = Image.new("RGBA", (2, 2))
image.putdata(RGBA_PIXELS)
image.save(os.path.join(OUT, "rgba.png"))
expect(os.path.join(OUT, "rgba.png"), "Pillow RGBA",
       gray=[luma(p[0], p[1], p[2]) for p in RGBA_PIXELS],
       alpha=[p[3] for p in RGBA_PIXELS], size=(2, 2))

PALETTE_PIXELS = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0), (10, 20, 30), (128, 128, 128)]
palette = []
for color in PALETTE_PIXELS:
    palette += list(color)
palette += [0] * (256 * 3 - len(palette))
image = Image.new("P", (3, 2))
image.putdata(list(range(6)))
image.putpalette(palette)
image.save(os.path.join(OUT, "palette.png"), optimize=False)
expect(os.path.join(OUT, "palette.png"), "Pillow palette (through the PLTE colours)",
       gray=[luma(*p) for p in PALETTE_PIXELS], size=(3, 2), alpha=None)

# 16 bits per channel: this build decodes 8, and has to say so rather than
# truncate the map.
image = Image.new("I;16", (2, 2))
image.putdata([0, 1000, 30000, 65535])
image.save(os.path.join(OUT, "deep16.png"))
expect(os.path.join(OUT, "deep16.png"), "Pillow I;16", refused="8-bit")

# ---- 2. the five row filters, from an encoder of our own -------------------
FILTER_ROWS = [bytes([0, 40, 90, 250]), bytes([10, 10, 10, 10])]
for filter_type in range(5):
    path = build(
        os.path.join(OUT, "filter%d.png" % filter_type), 4, 2, 0, 8,
        FILTER_ROWS, 1, filters=[filter_type, filter_type])
    expect(path, "a grayscale PNG whose rows use filter %d" % filter_type,
           gray=b"".join(FILTER_ROWS), size=(4, 2), alpha=None)

# All five in one file, which is what a real encoder does.
path = build(os.path.join(OUT, "filters.png"), 4, 5, 0, 8,
             [bytes([40, 41, 42, 43])] * 5, 1, filters=[0, 1, 2, 3, 4])
expect(path, "one file, a different filter per row",
       gray=bytes([40, 41, 42, 43] * 5), size=(4, 5), alpha=None)

# ---- 3. chunks that do not affect pixels ----------------------------------
path = build(
    os.path.join(OUT, "ancillary.png"), 4, 1, 0, 8, [bytes([1, 2, 3, 4])], 1,
    extra=[(b"gAMA", struct.pack(">I", 45455)),
           (b"tEXt", b"Software\x00the shim"),
           (b"pHYs", struct.pack(">IIB", 2835, 2835, 1))])
expect(path, "a PNG carrying gAMA / tEXt / pHYs", gray=[1, 2, 3, 4], size=(4, 1))

# ---- 4. what it has to refuse ---------------------------------------------
path = build(os.path.join(OUT, "interlaced.png"), 4, 2, 0, 8, FILTER_ROWS, 1, interlace=1)
expect(path, "an interlaced PNG", refused="interlaced")

path = build(os.path.join(OUT, "rgba16.png"), 2, 1, 6, 16,
             [bytes([0, 0, 0, 0, 255, 255, 255, 255])], 4)
expect(path, "a 16-bit RGBA PNG", refused="8-bit")

path = build(os.path.join(OUT, "depth4.png"), 4, 1, 3, 4, [bytes([0, 1, 2, 3])], 1)
expect(path, "a 4-bit paletted PNG", refused="8-bit")

data = SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 9, 0, 0, 0)) + chunk(b"IEND", b"")
open(os.path.join(OUT, "badtype.png"), "wb").write(data)
expect(os.path.join(OUT, "badtype.png"), "PNG colour type 9", refused="colour type 9")

open(os.path.join(OUT, "empty.png"), "wb").write(SIG)
expect(os.path.join(OUT, "empty.png"), "a PNG that is only its signature", refused="no IHDR")

# A file that is not a PNG and not a PGM either.
open(os.path.join(OUT, "garbage.bin"), "wb").write(b"not an image at all")
expect(os.path.join(OUT, "garbage.bin"), "a file that is neither PNG nor PGM",
       refused="neither a PNG nor a PGM")

print("\n%s (%d failure(s))" % ("PASSED" if failures == 0 else "FAILED", failures))
sys.exit(1 if failures else 0)