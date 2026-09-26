#!/usr/bin/env python3
from pathlib import Path
import re

data = Path("GAMES/DOOM/ENGINE.CLV").read_bytes()
code_size = int.from_bytes(data[8:12], "little")
entry = int.from_bytes(data[16:20], "little")
print("code_size", code_size, "entry", entry)
code = data[24 : 24 + code_size]
h = Path("compiler/clvm/clvm.h").read_text()
opmap = {int(v): n for n, v in re.findall(r"CL_OP_(\w+)\s*=\s*(\d+)", h)}
PUSH = [k for k, n in opmap.items() if n == "PUSH"][0]
STOREB = [k for k, n in opmap.items() if n == "STOREB"][0]
print("PUSH", PUSH, "STOREB", STOREB)

pc = entry
out = bytearray()
addrs = []
for step in range(500000):
    if pc + 11 > len(code):
        print("end pc", pc)
        break
    if code[pc] == PUSH and code[pc + 5] == PUSH and code[pc + 10] == STOREB:
        val = int.from_bytes(code[pc + 1 : pc + 5], "little", signed=True)
        addr = int.from_bytes(code[pc + 6 : pc + 10], "little", signed=True)
        out.append(val & 255)
        addrs.append(addr)
        pc += 11
    else:
        print(
            "stop at pc",
            pc,
            "op",
            opmap.get(code[pc]),
            "after",
            len(out),
            "bytes",
        )
        print("next", code[pc : pc + 16].hex())
        break

print(
    "str_init_bytes",
    len(out),
    "addr0",
    addrs[0] if addrs else None,
    "addrN",
    addrs[-1] if addrs else None,
)
text = bytes(out)
for needle in [
    b"doom: main",
    b"Doom Generic",
    b"Z_Init",
    b"doom: Z_Init",
    b"zone memory",
]:
    print(needle, text.find(needle))
print("head", text[:120])
i = text.find(b"doom")
print("first doom", i, text[i : i + 40] if i >= 0 else None)
i = text.find(b"do")
print("first do", i, text[i : i + 30] if i >= 0 else None)

# Check for embedded NULs inside would-be strings
if b"doom: main" in text:
    pass
else:
    # find near matches
    for i, c in enumerate(text):
        if c == ord("d") and i + 1 < len(text) and text[i + 1] == ord("o"):
            frag = text[i : i + 20]
            if b"do" in frag:
                print("do-frag", i, frag)
                if i > 5:
                    break
