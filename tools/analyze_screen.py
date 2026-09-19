#!/usr/bin/env python3
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "screen.ppm"
with open(path, "rb") as f:
    data = f.read()

i = data.find(b"255\n") + 4
pixels = data[i:]
w, h = 640, 480
bg = pixels[(40 * w + 100) * 3 : (40 * w + 100) * 3 + 3]
tb = sum(
    1
    for y in range(40)
    for x in range(w)
    if pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3] != bg
)
print("desktop_bg", bg, "taskbar_pixels", tb)
for y in [5, 20, 100, 240]:
    print("y", y, pixels[(y * w + 320) * 3 : (y * w + 320) * 3 + 3])
