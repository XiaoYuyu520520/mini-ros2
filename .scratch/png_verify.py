# -*- coding: utf-8 -*-
"""Independent check of the PNGs .scratch/png_check.exe wrote.

Walks the chunk list, recomputes every CRC with binascii, inflates the IDAT
with Python's zlib (not the C one the coder used) and rebuilds the pixels. Then
hands the files to Pillow, if Pillow is installed, for a decoder that shares no
code with either.
"""
import binascii
import os
import struct
import sys
import zlib

DIR = r"E:\DesktopData\app\code\CPU\.scratch\pngout"
SIG = b"\x89PNG\r\n\x1a\n"
failures = 0


def check(ok, what):
    global failures
    print("[%s] %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        failures += 1


def parse(path):
    data = open(path, "rb").read()
    assert data[:8] == SIG, path
    pos = 8
    chunks = []
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        (stored,) = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])
        computed = binascii.crc32(kind + payload) & 0xFFFFFFFF
        chunks.append((kind.decode(), payload, stored, computed))
        pos += 12 + length
    return data, chunks


def unfilter(raw, width, height, channels):
    stride = width * channels
    out = []
    previous = bytearray(stride)
    for y in range(height):
        line = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        filter_type = line[0]
        current = bytearray(line[1:])
        if filter_type == 1:
            for i in range(channels, stride):
                current[i] = (current[i] + current[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                current[i] = (current[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = current[i - channels] if i >= channels else 0
                current[i] = (current[i] + (left + previous[i]) // 2) & 0xFF
        elif filter_type == 4:
            def paeth(a, b, c):
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                return a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            for i in range(stride):
                left = current[i - channels] if i >= channels else 0
                up = previous[i]
                upleft = previous[i - channels] if i >= channels else 0
                current[i] = (current[i] + paeth(left, up, upleft)) & 0xFF
        elif filter_type != 0:
            raise AssertionError("filter %d" % filter_type)
        out.append(bytes(current))
        previous = current
    return out


def interleave(gray_rows, alpha_rows):
    return [bytes(v for pair in zip(g, a) for v in pair)
            for g, a in zip(gray_rows, alpha_rows)]


# gray.png: 4x3, one channel. matte.png: 2x2, gray then alpha per pixel. The
# levels and opacities are the ones png_check.cpp wrote.
GRAY_IN = [bytes([254] * 4), bytes([205] * 4), bytes([0] * 4)]
MATTE_GRAY_IN = [bytes([0, 255]), bytes([128, 255])]
MATTE_ALPHA_IN = [bytes([255, 255]), bytes([0, 255])]

for name, want_type, want_gray, want_alpha in (
        ("gray.png", 0, GRAY_IN, None),
        ("matte.png", 4, MATTE_GRAY_IN, MATTE_ALPHA_IN)):
    path = os.path.join(DIR, name)
    data, chunks = parse(path)
    check(all(c[2] == c[3] for c in chunks),
          "%s: every chunk CRC checks out (%d chunks)" % (name, len(chunks)))
    check(chunks[0][0] == "IHDR" and chunks[-1][0] == "IEND",
          "%s: IHDR first, IEND last" % name)

    ihdr = chunks[0][1]
    width, height = struct.unpack(">II", ihdr[:8])
    depth, colour, compression, filt, interlace = ihdr[8:13]
    check((depth, compression, filt, interlace) == (8, 0, 0, 0),
          "%s: 8-bit, deflate, adaptive filter, non-interlaced" % name)
    check(colour == want_type, "%s: colour type %d" % (name, want_type))

    idat = b"".join(c[1] for c in chunks if c[0] == "IDAT")
    raw = zlib.decompress(idat)
    channels = 2 if colour == 4 else 1
    check(len(raw) == (width * channels + 1) * height,
          "%s: IDAT inflates to %d scanlines of %d bytes"
          % (name, height, width * channels + 1))

    rows = unfilter(raw, width, height, channels)
    if colour == 0:
        check(rows == want_gray, "%s: the gray rows are the ones that went in" % name)
    else:
        gray = [bytes(row[i] for i in range(0, len(row), 2)) for row in rows]
        alpha = [bytes(row[i] for i in range(1, len(row), 2)) for row in rows]
        check(gray == want_gray, "%s: the gray rows are the ones that went in" % name)
        check(alpha == want_alpha, "%s: the alpha rows are the ones that went in" % name)
        check(gray[1][0] == 128 and alpha[1][0] == 0,
              "%s: the transparent cell kept both its level and its opacity" % name)
        check(interleave(gray, alpha) == rows,
              "%s: gray and alpha really are interleaved per pixel" % name)
    print("    %s: %d x %d, colour type %d, %d bytes on disk"
          % (name, width, height, colour, len(data)))

# A real decoder, if there is one: Pillow reads PNGs through libpng.
try:
    from PIL import Image as PilImage
except ImportError:
    print("[skipped] Pillow is not installed")
else:
    with PilImage.open(os.path.join(DIR, "gray.png")) as img:
        check(img.mode == "L" and img.size == (4, 3),
              "Pillow reads gray.png as L 4x3")
        check(list(img.getdata()) == [254] * 4 + [205] * 4 + [0] * 4,
              "Pillow sees the same gray levels")
    with PilImage.open(os.path.join(DIR, "matte.png")) as img:
        check(img.mode == "LA" and img.size == (2, 2),
              "Pillow reads matte.png as LA 2x2")
        check(list(img.getdata()) == [(0, 255), (255, 255), (128, 0), (255, 255)],
              "Pillow sees the same levels and opacities")

print("\n%s (%d failure(s))" % ("PASSED" if failures == 0 else "FAILED", failures))
sys.exit(1 if failures else 0)