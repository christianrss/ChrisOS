#include "chris_arch.h"

#include <string.h>

void chris_arch_reset(ChrisArchitectureState *st) {
    memset(st, 0, sizeof *st);
    st->rflags = 2ull;
    st->cr0 = CHRIS_CR0_PE | CHRIS_CR0_NE;
}

const char *chris_op_name(int op) {
    switch (op) {
    case CHRIS_OP_ALU:
        return "ALU";
    case CHRIS_OP_MOV:
        return "MOV";
    case CHRIS_OP_MOVZX:
        return "MOVZX";
    case CHRIS_OP_MOVSX:
        return "MOVSX";
    case CHRIS_OP_LEA:
        return "LEA";
    case CHRIS_OP_XCHG:
        return "XCHG";
    case CHRIS_OP_PUSH:
        return "PUSH";
    case CHRIS_OP_POP:
        return "POP";
    case CHRIS_OP_PUSHF:
        return "PUSHF";
    case CHRIS_OP_POPF:
        return "POPF";
    case CHRIS_OP_JMP:
        return "JMP";
    case CHRIS_OP_JCC:
        return "Jcc";
    case CHRIS_OP_CALL:
        return "CALL";
    case CHRIS_OP_RET:
        return "RET";
    case CHRIS_OP_SHIFT:
        return "SHIFT";
    case CHRIS_OP_UNARY:
        return "UNARY";
    case CHRIS_OP_MULDIV:
        return "MULDIV";
    case CHRIS_OP_IN:
        return "IN";
    case CHRIS_OP_OUT:
        return "OUT";
    case CHRIS_OP_INT:
        return "INT";
    case CHRIS_OP_IRETQ:
        return "IRETQ";
    case CHRIS_OP_HLT:
        return "HLT";
    case CHRIS_OP_NOP:
        return "NOP";
    case CHRIS_OP_FLAG:
        return "FLAG";
    case CHRIS_OP_LEAVE:
        return "LEAVE";
    case CHRIS_OP_CPUID:
        return "CPUID";
    case CHRIS_OP_RDMSR:
        return "RDMSR";
    case CHRIS_OP_WRMSR:
        return "WRMSR";
    case CHRIS_OP_MOVCR:
        return "MOVCR";
    case CHRIS_OP_DESC:
        return "DESC";
    case CHRIS_OP_SETCC:
        return "SETcc";
    case CHRIS_OP_CMOV:
        return "CMOVcc";
    case CHRIS_OP_UD:
        return "UD";
    case CHRIS_OP_UNIMPL:
        return "UNIMPL";
    default:
        return "NONE";
    }
}
