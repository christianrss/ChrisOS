#!/usr/bin/env python3
import struct
import re

text = open("compiler/clvm/clvm.h").read()
OPS = {}
for m in re.finditer(r"CL_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text):
    OPS[int(m.group(2), 0)] = "CL_OP_" + m.group(1)

LEN = {
    "CL_OP_PUSH": 5,
    "CL_OP_FPUSH": 5,
    "CL_OP_JMP": 3,
    "CL_OP_JZ": 3,
    "CL_OP_JNZ": 3,
    "CL_OP_CALL": 3,
    "CL_OP_LDARG": 2,
    "CL_OP_STLOC": 2,
    "CL_OP_LDLOC": 2,
    "CL_OP_NEWOBJ": 5,
    "CL_OP_LDFLD": 5,
    "CL_OP_STFLD": 5,
    "CL_OP_CALLT": 5,
    "CL_OP_LDSTR": 5,
    "CL_OP_JMP32": 5,
    "CL_OP_JZ32": 5,
    "CL_OP_JNZ32": 5,
    "CL_OP_CALL32": 5,
    "CL_OP_PUSH64": 9,
}

data = open("GAMES/DOOM/ENGINE.CLV", "rb").read()
code_size = struct.unpack_from("<I", data, 8)[0]
entry = struct.unpack_from("<I", data, 16)[0]
mem_hint = struct.unpack_from("<I", data, 20)[0]
code = data[24 : 24 + code_size]
print(f"entry={entry} code_size={code_size} mem_hint={mem_hint}")
print(f"op={OPS.get(code[entry])} bytes={list(code[entry:entry+12])}")

starts = set()
pc = 0
while pc < code_size:
    starts.add(pc)
    name = OPS.get(code[pc])
    n = LEN.get(name, 1)
    if n <= 0 or pc + n > code_size:
        print(f"bad at {pc} op={name or code[pc]}")
        break
    pc += n

print(f"starts={len(starts)} entry_in={entry in starts}")
if entry not in starts:
    below = max(s for s in starts if s < entry)
    bn = LEN.get(OPS.get(code[below]), 1)
    print(f"below={below} op={OPS.get(code[below])} len={bn} end={below+bn}")
