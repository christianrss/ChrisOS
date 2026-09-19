#!/usr/bin/env python3
with open("screen.ppm", "rb") as f:
    data = f.read()
i = data.find(b"255\n") + 4
pixels = data[i:]
w = 640
for y in [0, 5, 20, 39, 40]:
    for x in [20, 320, 500]:
        p = pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3]
        print(f"y={y} x={x} {list(p)}")
