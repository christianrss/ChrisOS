#!/usr/bin/env python3
import struct
import re

text = open("/mnt/e/Aulas/ChrisOS/compiler/clvm/clvm.h").read()
OPS = {
    int(m.group(2), 0): "CL_OP_" + m.group(1)
    for m in re.finditer(r"CL_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text)
}
LEN = {
    "CL_OP_PUSH": 5, "CL_OP_FPUSH": 5, "CL_OP_JMP": 3, "CL_OP_JZ": 3,
    "CL_OP_JNZ": 3, "CL_OP_CALL": 3, "CL_OP_LDARG": 2, "CL_OP_STLOC": 2,
    "CL_OP_LDLOC": 2, "CL_OP_NEWOBJ": 5, "CL_OP_LDFLD": 5, "CL_OP_STFLD": 5,
    "CL_OP_CALLT": 5, "CL_OP_LDSTR": 5, "CL_OP_JMP32": 5, "CL_OP_JZ32": 5,
    "CL_OP_JNZ32": 5, "CL_OP_CALL32": 5, "CL_OP_PUSH64": 9,
}
data = open("/mnt/e/Aulas/ChrisOS/GAMES/DOOM/ENGINE.CLV", "rb").read()
cs = struct.unpack_from("<I", data, 8)[0]
code = data[24 : 24 + cs]
starts = set()
p = 0
while p < cs:
    starts.add(p)
    p += LEN.get(OPS.get(code[p]), 1)
for pc in (131171, 131172):
    print("---", pc, OPS.get(code[pc]), "start", pc in starts)
    for i in range(pc - 20, pc + 12):
        if i in starts:
            print(i, OPS.get(code[i]), list(code[i : i + 8]))
