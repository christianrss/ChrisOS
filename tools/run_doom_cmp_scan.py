#!/usr/bin/env python3
"""Binary-search first PC where JIT SP diverges from interpreter (via cmp2)."""
import subprocess
import struct
import re
import sys

ROOT = "/mnt/e/Aulas/ChrisOS"
CMP = ROOT + "/build/host/test_doom_jit_cmp2"

text = open(ROOT + "/compiler/clvm/clvm.h").read()
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
data = open(ROOT + "/GAMES/DOOM/ENGINE.CLV", "rb").read()
cs = struct.unpack_from("<I", data, 8)[0]
code = data[24 : 24 + cs]
starts = []
p = 0
while p < cs:
    starts.append(p)
    p += LEN.get(OPS.get(code[p]), 1)

def probe(pc):
    r = subprocess.run([CMP, str(pc)], capture_output=True, text=True, cwd=ROOT)
    out = r.stdout + r.stderr
    if "interp FAULT" in out or "interp never" in out or "interp HALT" in out:
        return "unreachable", out
    if "nat=0" in out:
        return "nat0", out
    if "match" in out:
        return "match", out
    if "DIVERGE" in out:
        return "diverge", out
    return "other", out

# Candidate PCs from run_jit_cmp + fault region + early
candidates = [
    434229, 434400, 434500, 435000, 440000, 450000, 500000,
    283814, 205820, 7210, 7218, 131142, 131159, 131171, 131172,
    427277, 430940, 463940,
]
# filter to insn starts
cset = set(starts)
candidates = [c for c in candidates if c in cset]

for pc in candidates:
    kind, out = probe(pc)
    print("=== %u %s ===" % (pc, kind))
    print(out.strip().split("\n")[-6:])
    sys.stdout.flush()
