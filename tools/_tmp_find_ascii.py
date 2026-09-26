#!/usr/bin/env python3
d = open("GAMES/DOOM/ENGINE.CLV", "rb").read()
# find contiguous ASCII "Generic" or "0.1"
for s in [b"Generic", b"0.1", b"Doom", b"free software", b"W_Init", b"V_Init"]:
    print(repr(s), d.find(s))
