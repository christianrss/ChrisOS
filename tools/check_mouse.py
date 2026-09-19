#!/usr/bin/env python3
"""Check taskbar and mouse cursor in a QEMU screendump PPM."""
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "screen.ppm"
with open(path, "rb") as f:
    data = f.read()

i = data.find(b"255\n") + 4
pixels = data[i:]
w, h = 640, 480

bg = pixels[(240 * w + 100) * 3 : (240 * w + 100) * 3 + 3]
tb = sum(
    1
    for y in range(40)
    for x in range(w)
    if pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3] != bg
)
print("desktop_bg", bg, "taskbar_pixels", tb)

cx, cy = 320, 240
found = []
for dy in range(-12, 13):
    for dx in range(-12, 13):
        x, y = cx + dx, cy + dy
        if 0 <= x < w and 0 <= y < h:
            p = pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3]
            if p != bg and p != b"\x00}\x00":
                found.append((x, y, p))
print("cursor_area_non_bg", len(found))
if found[:8]:
    print("samples", found[:8])

bright = []
for y in range(h):
    for x in range(w):
        p = pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3]
        if p[0] > 200 and p[1] > 200 and p[2] > 200:
            bright.append((x, y))
print("near_white_pixels", len(bright))
if bright:
    xs = [b[0] for b in bright]
    ys = [b[1] for b in bright]
    print("bright_bbox", min(xs), min(ys), max(xs), max(ys))
