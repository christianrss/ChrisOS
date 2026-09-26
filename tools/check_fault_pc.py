#!/usr/bin/env python3
import struct

b = open("GAMES/DOOM/ENGINE.CLV", "rb").read()
code = b[24:]
ops = {
    0: "NOP", 1: "PUSH", 2: "ADD", 3: "SUB", 4: "MUL", 5: "DIV", 6: "DUP",
    7: "PRINT", 8: "HALT", 9: "JMP", 10: "JZ", 11: "CALL", 12: "RET",
    13: "LOAD", 14: "STORE", 15: "DROP", 16: "SWAP", 17: "EQ", 18: "LT",
    19: "JNZ", 20: "MOD", 28: "CALLI", 32: "SYS", 33: "JMP32", 34: "JZ32",
    35: "JNZ32", 36: "CALL32", 37: "PUSH64", 40: "FLOAD", 41: "FSTORE",
    42: "FPUSH", 60: "LDARG", 61: "STLOC", 62: "LDLOC",
}

def insn_len(pc):
    if pc >= len(code):
        return 0
    op = code[pc]
    if op in (1, 42):
        return 5
    if op in (9, 10, 19, 11):
        return 3
    if op in (60, 61, 62):
        return 2
    if op in (33, 34, 35, 36) or op in range(63, 68):
        return 5
    if op == 37:
        return 9
    return 1

pc = 130000
starts = {}
while pc < 131250 and pc < len(code):
    n = insn_len(pc)
    if n <= 0:
        break
    starts[pc] = n
    pc += n

for p in sorted(starts):
    if p < 131150 or p > 131220:
        continue
    op = code[p]
    name = ops.get(op, f"op{op}")
    extra = ""
    if op == 1:
        extra = " imm=%d" % struct.unpack_from("<i", code, p + 1)[0]
    elif op == 42:
        extra = " f=%s" % struct.unpack_from("<f", code, p + 1)[0]
    elif op in (9, 10, 19, 11):
        extra = " tgt=%d" % struct.unpack_from("<h", code, p + 1)[0]
    elif op in (33, 34, 35, 36):
        extra = " tgt=%d" % struct.unpack_from("<i", code, p + 1)[0]
    elif op == 32:
        # SYS is just opcode; arg often pushed before
        pass
    print("pc=%d %s%s len=%d" % (p, name, extra, starts[p]))

print("entry", struct.unpack_from("<I", b, 16)[0], "code_size", len(code))
print("fault enums: 4=STACK_UNDERFLOW")
