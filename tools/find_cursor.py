#!/usr/bin/env python3
from collections import Counter

with open("screen.ppm", "rb") as f:
    data = f.read()
i = data.find(b"255\n") + 4
pixels = data[i:]
w, h = 640, 480

def px(x, y):
    return pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3]

colors = Counter()
for y in range(h):
    for x in range(w):
        colors[px(x, y)] += 1

green = sum(
    1
    for y in range(40)
    for x in range(w)
    if pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3] == bytes([0, 125, 0])
)
print("green_taskbar_pixels", green)
print("top colors:")
for c, n in colors.most_common(8):
    print(n, list(c))

# pixels that are not top 3 colors
common = {c for c, _ in colors.most_common(4)}
odd = [(x, y, px(x, y)) for y in range(h) for x in range(w) if px(x, y) not in common]
print("uncommon pixels", len(odd))
if odd:
    xs = [p[0] for p in odd]
    ys = [p[1] for p in odd]
    print("bbox", min(xs), min(ys), max(xs), max(ys))
