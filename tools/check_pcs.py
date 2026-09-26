#!/usr/bin/env python3
import struct
import re

text = open("/mnt/e/Aulas/ChrisOS/compiler/clvm/clvm.h").read()
OPS = {}
for m in re.finditer(r"CL_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text):
    OPS[int(m.group(2), 0)] = "CL_OP_" + m.group(1)
LEN = {
    "CL_OP_PUSH": 5, "CL_OP_FPUSH": 5, "CL_OP_JMP": 3, "CL_OP_JZ": 3,
    "CL_OP_JNZ": 3, "CL_OP_CALL": 3, "CL_OP_LDARG": 2, "CL_OP_STLOC": 2,
    "CL_OP_LDLOC": 2, "CL_OP_NEWOBJ": 5, "CL_OP_LDFLD": 5, "CL_OP_STFLD": 5,
    "CL_OP_CALLT": 5, "CL_OP_LDSTR": 5, "CL_OP_JMP32": 5, "CL_OP_JZ32": 5,
    "CL_OP_JNZ32": 5, "CL_OP_CALL32": 5, "CL_OP_PUSH64": 9,
}
data = open("/mnt/e/Aulas/ChrisOS/GAMES/DOOM/ENGINE.CLV", "rb").read()
code_size = struct.unpack_from("<I", data, 8)[0]
code = data[24 : 24 + code_size]
for pc in (129339, 129326, 278265, 820901):
    if pc >= code_size:
        print(pc, "oob")
        continue
    print(pc, OPS.get(code[pc], code[pc]), list(code[pc : pc + 12]))
