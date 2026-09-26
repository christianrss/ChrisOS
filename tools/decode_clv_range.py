#!/usr/bin/env python3
import struct
import re
import sys

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

# Find calls to 130841 and function body until RET at depth 0
start = int(sys.argv[1]) if len(sys.argv) > 1 else 130841
end = int(sys.argv[2]) if len(sys.argv) > 2 else 131250
p = start
while p < end and p < cs:
    op = OPS.get(code[p], "?%02x" % code[p])
    n = LEN.get(op, 1)
    extra = ""
    if op in ("PUSH", "FPUSH"):
        extra = " imm=%d" % struct.unpack_from("<i", code, p + 1)[0]
    elif op in ("JMP32", "JZ32", "JNZ32", "CALL32"):
        rel = struct.unpack_from("<i", code, p + 1)[0]
        extra = " tgt=%d" % (p + n + rel)
    elif op in ("JMP", "JZ", "JNZ", "CALL"):
        rel = struct.unpack_from("<h", code, p + 1)[0]
        extra = " tgt=%d" % (p + n + rel)
    elif op == "SYS":
        extra = ""
    print("%d: %s%s" % (p, op, extra))
    if op == "RET" and p > start:
        # keep going until end; don't break - nested funcs
        pass
    p += n
