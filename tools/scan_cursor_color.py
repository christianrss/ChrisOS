#!/usr/bin/env python3
with open("screen.ppm", "rb") as f:
    data = f.read()
i = data.find(b"255\n") + 4
pixels = data[i:]
w, h = 640, 480
target = bytes([123, 123, 255])
hits = []
for y in range(h):
    for x in range(w):
        p = pixels[(y * w + x) * 3 : (y * w + x) * 3 + 3]
        if p == target:
            hits.append((x, y))
print("hits", len(hits))
if hits:
    xs = [h[0] for h in hits]
    ys = [h[1] for h in hits]
    print("bbox", min(xs), min(ys), max(xs), max(ys))
