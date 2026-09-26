#!/usr/bin/env python3
d = open("GAMES/DOOM/ENGINE.CLV", "rb").read()
for s in [b"Doom Generic", b"Z_Init", b"zone memory", b"Init zone", b"DOOM"]:
    print(s, d.find(s))
# Also show string pool region - last 2k printable
tail = d[-4000:]
chars = []
for b in tail:
    if 32 <= b < 127:
        chars.append(chr(b))
    elif b == 0:
        chars.append("|")
    else:
        chars.append(".")
print("".join(chars)[:1500])
