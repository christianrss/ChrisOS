#include "il.h"
#include "../clvm/clvm.h"

static int push_t(IlType *st, int *sp, int max, IlType t) {
    if (*sp >= max)
        return 0;
    st[(*sp)++] = t;
    return 1;
}

static int pop_t(IlType *st, int *sp, IlType *out) {
    if (*sp < 1)
        return 0;
    *out = st[--*sp];
    return 1;
}

static IlType bin_t(IlType a, IlType b) {
    if (a == IL_F8 || b == IL_F8)
        return IL_F8;
    if (a == IL_F4 || b == IL_F4)
        return IL_F4;
    if (a == IL_I8 || b == IL_I8 || a == IL_PTR || b == IL_PTR)
        return IL_I8;
    if (a == IL_REF)
        return IL_REF;
    return IL_I4;
}

int il_verify(const uint8_t *code, uint32_t size, const IlSig *sig) {
    uint32_t pc = 0;
    IlType st[256];
    int sp = 0;
    int maxd = 256;
    IlType tmp;
    IlType tmp2;

    if (!code)
        return 0;
    while (pc < size) {
        uint8_t op = code[pc++];
        switch (op) {
        case CL_OP_NOP:
        case CL_OP_SAFEPOINT:
        case CL_OP_HALT:
            break;
        case CL_OP_RET:
            if (sp > 0)
                (void)pop_t(st, &sp, &tmp);
            break;
        case CL_OP_PUSH:
            if (pc + 4 > size)
                return 0;
            pc += 4;
            if (!push_t(st, &sp, maxd, IL_I4))
                return 0;
            break;
        case CL_OP_FPUSH:
            if (pc + 4 > size)
                return 0;
            pc += 4;
            if (!push_t(st, &sp, maxd, IL_F4))
                return 0;
            break;
        case CL_OP_PUSH64:
            if (pc + 8 > size)
                return 0;
            pc += 8;
            if (!push_t(st, &sp, maxd, IL_I8))
                return 0;
            break;
        case CL_OP_ADD:
        case CL_OP_SUB:
        case CL_OP_MUL:
        case CL_OP_DIV:
        case CL_OP_MOD:
        case CL_OP_AND:
        case CL_OP_OR:
        case CL_OP_XOR:
        case CL_OP_SHL:
        case CL_OP_SHR:
        case CL_OP_SAR:
        case CL_OP_EQ:
        case CL_OP_NE:
        case CL_OP_LT:
        case CL_OP_LE:
        case CL_OP_GT:
        case CL_OP_GE:
        case CL_OP_FADD:
        case CL_OP_FSUB:
        case CL_OP_FMUL:
        case CL_OP_FDIV:
        case CL_OP_FEQ:
        case CL_OP_FLT:
        case CL_OP_FLE:
            if (!pop_t(st, &sp, &tmp) || !pop_t(st, &sp, &tmp2))
                return 0;
            if (!push_t(st, &sp, maxd, bin_t(tmp, tmp2)))
                return 0;
            break;
        case CL_OP_NEG:
        case CL_OP_NOT:
        case CL_OP_FNEG:
        case CL_OP_FTOI:
        case CL_OP_ITOF:
            if (!pop_t(st, &sp, &tmp))
                return 0;
            if (op == CL_OP_ITOF)
                tmp = IL_F4;
            else if (op == CL_OP_FTOI)
                tmp = IL_I4;
            if (!push_t(st, &sp, maxd, tmp))
                return 0;
            break;
        case CL_OP_DUP:
            if (sp < 1 || !push_t(st, &sp, maxd, st[sp - 1]))
                return 0;
            break;
        case CL_OP_DROP:
            if (!pop_t(st, &sp, &tmp))
                return 0;
            break;
        case CL_OP_SWAP:
            if (sp < 2)
                return 0;
            tmp = st[sp - 1];
            st[sp - 1] = st[sp - 2];
            st[sp - 2] = tmp;
            break;
        case CL_OP_LDARG:
        case CL_OP_LDLOC:
            if (pc >= size)
                return 0;
            pc++;
            tmp = IL_I4;
            if (sig && op == CL_OP_LDARG && code[pc - 1] < sig->argc)
                tmp = sig->args[code[pc - 1]];
            if (!push_t(st, &sp, maxd, tmp))
                return 0;
            break;
        case CL_OP_STLOC:
            if (pc >= size || !pop_t(st, &sp, &tmp))
                return 0;
            pc++;
            break;
        case CL_OP_NEWOBJ:
        case CL_OP_LDSTR:
            if (pc + 4 > size)
                return 0;
            pc += 4;
            if (!push_t(st, &sp, maxd, op == CL_OP_LDSTR ? IL_PTR : IL_REF))
                return 0;
            break;
        case CL_OP_CALLT:
            if (pc + 4 > size)
                return 0;
            pc += 4;
            if (sig && sig->argc) {
                uint8_t i;
                for (i = 0; i < sig->argc; ++i) {
                    if (!pop_t(st, &sp, &tmp))
                        return 0;
                }
            }
            if (sig && sig->ret != IL_VOID &&
                !push_t(st, &sp, maxd, sig->ret))
                return 0;
            break;
        case CL_OP_LDFLD:
            if (pc + 4 > size || !pop_t(st, &sp, &tmp))
                return 0;
            pc += 4;
            if (tmp != IL_REF && tmp != IL_PTR && tmp != IL_I8 && tmp != IL_I4)
                return 0;
            if (!push_t(st, &sp, maxd, IL_I4))
                return 0;
            break;
        case CL_OP_STFLD:
            if (pc + 4 > size || !pop_t(st, &sp, &tmp) ||
                !pop_t(st, &sp, &tmp2))
                return 0;
            pc += 4;
            break;
        case CL_OP_LOAD:
        case CL_OP_FLOAD:
        case CL_OP_LOADB:
        case CL_OP_LOAD64:
            if (!pop_t(st, &sp, &tmp))
                return 0;
            tmp = (op == CL_OP_LOAD64) ? IL_I8 :
                  (op == CL_OP_FLOAD) ? IL_F4 :
                  (op == CL_OP_LOADB) ? IL_I4 : IL_I4;
            if (!push_t(st, &sp, maxd, tmp))
                return 0;
            break;
        case CL_OP_STORE:
        case CL_OP_FSTORE:
        case CL_OP_STOREB:
        case CL_OP_STORE64:
            if (!pop_t(st, &sp, &tmp) || !pop_t(st, &sp, &tmp2))
                return 0;
            break;
        case CL_OP_JMP:
        case CL_OP_CALL:
            if (pc + 2 > size)
                return 0;
            pc += 2;
            break;
        case CL_OP_JZ:
        case CL_OP_JNZ:
            if (pc + 2 > size || !pop_t(st, &sp, &tmp))
                return 0;
            pc += 2;
            break;
        case CL_OP_JMP32:
        case CL_OP_CALL32:
            if (pc + 4 > size)
                return 0;
            pc += 4;
            break;
        case CL_OP_JZ32:
        case CL_OP_JNZ32:
            if (pc + 4 > size || !pop_t(st, &sp, &tmp))
                return 0;
            pc += 4;
            break;
        case CL_OP_SYS:
        case CL_OP_PRINT:
            break;
        default:
            break;
        }
    }
    return 1;
}
