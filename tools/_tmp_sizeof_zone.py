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

# Find Z_Init-ish: after large pattern, look for PUSH of small struct sizes near STORE64 of zone
# Search for PUSH 48 / PUSH 40 / PUSH 36 (likely sizeof memzone)
for sz in (24, 32, 36, 40, 48, 56, 64):
    hits = []
    p = 0
    while p < cs:
        op = OPS.get(code[p], "?")
        n = LEN.get(op, 1)
        if op == "PUSH" and p + 5 <= cs:
            imm = struct.unpack_from("<i", code, p + 1)[0]
            if imm == sz:
                hits.append(p)
        p += n
    print("PUSH", sz, "count", len(hits), "first", hits[:5])

# Decode around guest malloc call site - SYS 56 with big size is dynamic.
# Find Z_Init by looking for call after banner - search PUSH string offsets?
# Simpler: find function that does ADD of sizeof after zone pointer
print("\nLook for pattern: LOAD64; PUSH imm; ADD; STORE64 near 130000+")
