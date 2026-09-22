#!/usr/bin/env python3
import struct
import re

text = open("compiler/clvm/clvm.h").read()
OPS = {
    int(m.group(2), 0): m.group(1)
    for m in re.finditer(r"CL_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text)
}
LEN = {
    "PUSH": 5, "FPUSH": 5, "JMP": 3, "JZ": 3, "JNZ": 3, "CALL": 3,
    "LDARG": 2, "STLOC": 2, "LDLOC": 2, "NEWOBJ": 5, "LDFLD": 5, "STFLD": 5,
    "CALLT": 5, "LDSTR": 5, "JMP32": 5, "JZ32": 5, "JNZ32": 5, "CALL32": 5,
    "PUSH64": 9,
}
data = open("GAMES/DOOM/ENGINE.CLV", "rb").read()
cs = struct.unpack_from("<I", data, 8)[0]
code = data[24 : 24 + cs]

# Find PUSH 128 near PUSH 88 patterns (sha1 pad 0x80)
p = 0
while p < cs:
    op = OPS.get(code[p], "?")
    n = LEN.get(op, 1)
    if op == "PUSH" and p + 5 <= cs:
        imm = struct.unpack_from("<i", code, p + 1)[0]
        if imm == 128:
            # look ahead for PUSH 88 within 80 bytes
            q = p
            saw88 = False
            seq = []
            for _ in range(25):
                if q >= cs:
                    break
                o = OPS.get(code[q], "?")
                nn = LEN.get(o, 1)
                ex = ""
                if o in ("PUSH", "FPUSH"):
                    v = struct.unpack_from("<i", code, q + 1)[0]
                    ex = " %d" % v
                    if v == 88:
                        saw88 = True
                elif o in ("JMP32", "JZ32", "JNZ32"):
                    rel = struct.unpack_from("<i", code, q + 1)[0]
                    ex = " ->%d" % (q + nn + rel)
                seq.append("%s%s" % (o, ex))
                q += nn
            if saw88:
                print("at", p, ":", " | ".join(seq))
                print()
    p += n
