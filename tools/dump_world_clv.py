#!/usr/bin/env python3
import struct
b = open("GAMES/WORLD.CLV", "rb").read()
hdr = 24
code = b[hdr:]
print("file", len(b), "code", len(code), "entry", struct.unpack_from("<I", b, 16)[0])
i = 0
n = 0
while i < len(code) and n < 120:
    op = code[i]
    if op == 1:  # PUSH
        v = struct.unpack_from("<i", code, i + 1)[0]
        print(f"{i}: PUSH {v}")
        i += 5
    elif op == 0x19:  # FPUSH?
        # check enum
        print(f"{i}: op{op:#x} ...")
        i += 1
    elif op in (9, 10, 0x13, 0x0B):
        t = struct.unpack_from("<h", code, i + 1)[0]
        print(f"{i}: op{op} tgt={t}")
        i += 3
    elif op == 0x1A:  # maybe
        print(f"{i}: op{op}")
        i += 1
    else:
        print(f"{i}: op{op}")
        i += 1
    n += 1
