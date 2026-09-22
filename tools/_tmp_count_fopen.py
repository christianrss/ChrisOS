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
p = 0
n = 0
while p < cs:
    op = OPS.get(code[p], "?")
    ln = LEN.get(op, 1)
    if op == "PUSH" and p + 6 <= cs and OPS.get(code[p + 5]) == "SYS":
        imm = struct.unpack_from("<i", code, p + 1)[0]
        if imm == 50:
            n += 1
            if n <= 8:
                print("fopen sys at", p)
    p += ln
print("total fopen SYS", n)
