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

# Find PUSH 56 near SHA1 pad (was ~14866)
hits = []
p = 0
while p < cs:
    op = OPS.get(code[p], "?")
    n = LEN.get(op, 1)
    if op == "PUSH" and p + 5 <= cs:
        imm = struct.unpack_from("<i", code, p + 1)[0]
        if imm == 56:
            hits.append(p)
    p += n
print("PUSH 56 at", hits[:10])
# decode around first that looks like count compare after arg setup
for start in hits[:3]:
    print("\n=== around", start, "===")
    p = start - 40
    if p < 0:
        p = 0
    for _ in range(45):
        if p >= cs:
            break
        op = OPS.get(code[p], "?")
        n = LEN.get(op, 1)
        extra = ""
        if op in ("PUSH", "FPUSH"):
            extra = " imm=%d" % struct.unpack_from("<i", code, p + 1)[0]
        elif op in ("JMP32", "JZ32", "JNZ32", "CALL32"):
            rel = struct.unpack_from("<i", code, p + 1)[0]
            extra = " tgt=%d" % (p + n + rel)
        mark = " <<<" if p == start else ""
        print("%d: %s%s%s" % (p, op, extra, mark))
        p += n
