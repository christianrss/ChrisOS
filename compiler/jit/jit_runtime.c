#include "jit_runtime.h"

#ifdef __freestanding__
#include "clvm_sys.h"
#include "gfx2d.h"
#include "jit.h"
#else
#include "clvm_sys.h"
#include "gfx2d.h"

static uint32_t g_host_px[320 * 200];
#endif

static int fault(ClvmVm *vm, ClvmFault f, uint32_t pc) {
    vm->fault = f;
    vm->fault_pc = pc;
    vm->state = CLVM_FAULTED;
    return -1;
}

int jit_rt_fetch_u32(ClvmVm *vm, uint32_t *out) {
    uint32_t pc;
    if (vm == 0 || out == 0)
        return -1;
    pc = vm->pc;
    if (pc + 4u > vm->code_size)
        return fault(vm, CLVM_FAULT_TRUNCATED, pc);
    *out = (uint32_t)vm->code[pc] |
           ((uint32_t)vm->code[pc + 1u] << 8) |
           ((uint32_t)vm->code[pc + 2u] << 16) |
           ((uint32_t)vm->code[pc + 3u] << 24);
    vm->pc = pc + 4u;
    return 0;
}

int jit_rt_fetch_i16(ClvmVm *vm, int16_t *out) {
    uint32_t pc;
    if (vm == 0 || out == 0)
        return -1;
    pc = vm->pc;
    if (pc + 2u > vm->code_size)
        return fault(vm, CLVM_FAULT_TRUNCATED, pc);
    *out = (int16_t)((uint16_t)vm->code[pc] |
                     ((uint16_t)vm->code[pc + 1u] << 8));
    vm->pc = pc + 2u;
    return 0;
}

int jit_rt_push(ClvmVm *vm, int32_t v) {
    if (vm == 0)
        return -1;
    if (vm->sp >= CLVM_STACK_MAX)
        return fault(vm, CLVM_FAULT_STACK_OVERFLOW, vm->pc);
    vm->stack[vm->sp++] = v;
    return 0;
}

int jit_rt_pop(ClvmVm *vm, int32_t *out) {
    if (vm == 0 || out == 0)
        return -1;
    if (vm->sp == 0)
        return fault(vm, CLVM_FAULT_STACK_UNDERFLOW, vm->pc);
    *out = vm->stack[--vm->sp];
    return 0;
}

static int jit_rt_fbinop(ClvmVm *vm, uint8_t op) {
    int32_t a;
    int32_t b;
    union {
        uint32_t u;
        float f;
    } fa;
    union {
        uint32_t u;
        float f;
    } fb;
    union {
        uint32_t u;
        float f;
    } fr;

    if (jit_rt_pop(vm, &b) != 0 || jit_rt_pop(vm, &a) != 0)
        return -1;
    fa.u = (uint32_t)a;
    fb.u = (uint32_t)b;
    if (op == CL_OP_FDIV && fb.f == 0.0f)
        return fault(vm, CLVM_FAULT_DIV_ZERO, vm->pc);
    if (op == CL_OP_FADD)
        fr.f = fa.f + fb.f;
    else if (op == CL_OP_FSUB)
        fr.f = fa.f - fb.f;
    else if (op == CL_OP_FMUL)
        fr.f = fa.f * fb.f;
    else if (op == CL_OP_FDIV)
        fr.f = fa.f / fb.f;
    else if (op == CL_OP_FEQ)
        return jit_rt_push(vm, fa.f == fb.f);
    else if (op == CL_OP_FLT)
        return jit_rt_push(vm, fa.f < fb.f);
    else
        return jit_rt_push(vm, fa.f <= fb.f);
    return jit_rt_push(vm, (int32_t)fr.u);
}

int jit_rt_binop(ClvmVm *vm, uint8_t op) {
    int32_t a;
    int32_t b;
    int32_t r;

    if (jit_rt_pop(vm, &b) != 0 || jit_rt_pop(vm, &a) != 0)
        return -1;
    if ((op == CL_OP_DIV || op == CL_OP_MOD) && b == 0)
        return fault(vm, CLVM_FAULT_DIV_ZERO, vm->pc);
    if ((op == CL_OP_DIV || op == CL_OP_MOD) &&
        a == (int32_t)0x80000000u && b == -1)
        return fault(vm, CLVM_FAULT_DIV_OVERFLOW, vm->pc);

    switch (op) {
    case CL_OP_ADD:
        r = (int32_t)((uint32_t)a + (uint32_t)b);
        break;
    case CL_OP_SUB:
        r = (int32_t)((uint32_t)a - (uint32_t)b);
        break;
    case CL_OP_MUL:
        r = (int32_t)((uint32_t)a * (uint32_t)b);
        break;
    case CL_OP_DIV:
        r = a / b;
        break;
    case CL_OP_MOD:
        r = a % b;
        break;
    case CL_OP_EQ:
        r = a == b;
        break;
    case CL_OP_NE:
        r = a != b;
        break;
    case CL_OP_LT:
        r = a < b;
        break;
    case CL_OP_LE:
        r = a <= b;
        break;
    case CL_OP_GT:
        r = a > b;
        break;
    case CL_OP_GE:
        r = a >= b;
        break;
    default:
        return fault(vm, CLVM_FAULT_OPCODE, vm->pc);
    }
    return jit_rt_push(vm, r);
}

static int jump_rel(ClvmVm *vm, int16_t rel) {
    int64_t target = (int64_t)vm->pc + rel;
    if (target < 0 || target >= (int64_t)vm->code_size)
        return fault(vm, CLVM_FAULT_BAD_JUMP, vm->pc);
    vm->pc = (uint32_t)target;
    return 0;
}

int jit_rt_sys(ClvmVm *vm) {
    int32_t id;

    if (jit_rt_pop(vm, &id) != 0)
        return -1;

    if (id == 1) {
        int32_t a;
        int32_t b;
        int32_t c;
        if (jit_rt_pop(vm, &c) != 0 || jit_rt_pop(vm, &b) != 0 ||
            jit_rt_pop(vm, &a) != 0)
            return -1;
#ifdef __freestanding__
        {
            ClvmGfxCtx *ctx = (ClvmGfxCtx *)vm->sys_user;
            if (ctx == 0 || ctx->pixels == 0)
                return fault(vm, CLVM_FAULT_BAD_SYS, vm->pc);
            gfx2d_put(ctx->pixels, ctx->w, ctx->h, a, b, c);
        }
#else
        if (a >= 0 && b >= 0 && a < 320 && b < 200)
            g_host_px[b * 320 + a] = gfx2d_color(c);
#endif
        return 0;
    }

    if (vm->sys == 0 || vm->sys(vm, id, vm->sys_user) != 0)
        return fault(vm, CLVM_FAULT_BAD_SYS, vm->pc);
    return 0;
}

static int jit_rt_exec_op(ClvmVm *vm, uint8_t op) {
    uint32_t u;
    int16_t rel;
    int32_t a;
    int32_t b;

    if (vm == 0)
        return -1;

    switch (op) {
    case CL_OP_NOP:
        return 0;
    case CL_OP_PUSH:
    case CL_OP_FPUSH:
        if (jit_rt_fetch_u32(vm, &u) != 0)
            return -1;
        return jit_rt_push(vm, (int32_t)u);
    case CL_OP_FADD:
    case CL_OP_FSUB:
    case CL_OP_FMUL:
    case CL_OP_FDIV:
    case CL_OP_FEQ:
    case CL_OP_FLT:
    case CL_OP_FLE:
        return jit_rt_fbinop(vm, op);
    case CL_OP_FNEG: {
        union {
            uint32_t u;
            float f;
        } v;
        if (jit_rt_pop(vm, &a) != 0)
            return -1;
        v.u = (uint32_t)a;
        v.f = -v.f;
        return jit_rt_push(vm, (int32_t)v.u);
    }
    case CL_OP_FTOI: {
        union {
            uint32_t u;
            float f;
        } v;
        if (jit_rt_pop(vm, &a) != 0)
            return -1;
        v.u = (uint32_t)a;
        return jit_rt_push(vm, (int32_t)v.f);
    }
    case CL_OP_ITOF: {
        union {
            uint32_t u;
            float f;
        } v;
        if (jit_rt_pop(vm, &a) != 0)
            return -1;
        v.f = (float)a;
        return jit_rt_push(vm, (int32_t)v.u);
    }
    case CL_OP_ADD:
    case CL_OP_SUB:
    case CL_OP_MUL:
    case CL_OP_DIV:
    case CL_OP_MOD:
    case CL_OP_EQ:
    case CL_OP_NE:
    case CL_OP_LT:
    case CL_OP_LE:
    case CL_OP_GT:
    case CL_OP_GE:
        return jit_rt_binop(vm, op);
    case CL_OP_NEG:
        if (jit_rt_pop(vm, &a) != 0)
            return -1;
        return jit_rt_push(vm, (int32_t)(0u - (uint32_t)a));
    case CL_OP_DUP:
        if (vm->sp == 0)
            return fault(vm, CLVM_FAULT_STACK_UNDERFLOW, vm->pc);
        return jit_rt_push(vm, vm->stack[vm->sp - 1u]);
    case CL_OP_SWAP:
        if (vm->sp < 2u)
            return fault(vm, CLVM_FAULT_STACK_UNDERFLOW, vm->pc);
        a = vm->stack[vm->sp - 1u];
        vm->stack[vm->sp - 1u] = vm->stack[vm->sp - 2u];
        vm->stack[vm->sp - 2u] = a;
        return 0;
    case CL_OP_DROP:
    case CL_OP_PRINT:
        return jit_rt_pop(vm, &a);
    case CL_OP_CALL:
        if (jit_rt_fetch_i16(vm, &rel) != 0)
            return -1;
        if (vm->csp >= CLVM_CALL_MAX)
            return fault(vm, CLVM_FAULT_CALL_OVERFLOW, vm->pc);
        vm->calls[vm->csp++] = vm->pc;
        return jump_rel(vm, rel);
    case CL_OP_RET:
        if (vm->csp == 0u)
            return fault(vm, CLVM_FAULT_CALL_UNDERFLOW, vm->pc);
        vm->pc = vm->calls[--vm->csp];
        return 0;
    case CL_OP_LOAD:
    case CL_OP_FLOAD:
        if (jit_rt_pop(vm, &a) != 0)
            return -1;
        if (a < 0 || (uint32_t)a > CLVM_MEMORY_SIZE - 4u)
            return fault(vm, CLVM_FAULT_BAD_ADDRESS, vm->pc);
        u = (uint32_t)vm->memory[(uint32_t)a] |
            ((uint32_t)vm->memory[(uint32_t)a + 1u] << 8) |
            ((uint32_t)vm->memory[(uint32_t)a + 2u] << 16) |
            ((uint32_t)vm->memory[(uint32_t)a + 3u] << 24);
        return jit_rt_push(vm, (int32_t)u);
    case CL_OP_STORE:
    case CL_OP_FSTORE:
        if (jit_rt_pop(vm, &a) != 0 || jit_rt_pop(vm, &b) != 0)
            return -1;
        if (b < 0 || (uint32_t)b > CLVM_MEMORY_SIZE - 4u)
            return fault(vm, CLVM_FAULT_BAD_ADDRESS, vm->pc);
        u = (uint32_t)a;
        vm->memory[(uint32_t)b] = (uint8_t)(u & 0xffu);
        vm->memory[(uint32_t)b + 1u] = (uint8_t)((u >> 8) & 0xffu);
        vm->memory[(uint32_t)b + 2u] = (uint8_t)((u >> 16) & 0xffu);
        vm->memory[(uint32_t)b + 3u] = (uint8_t)((u >> 24) & 0xffu);
        return 0;
    case CL_OP_JMP:
        if (jit_rt_fetch_i16(vm, &rel) != 0)
            return -1;
        return jump_rel(vm, rel);
    case CL_OP_JZ:
    case CL_OP_JNZ:
        if (jit_rt_fetch_i16(vm, &rel) != 0 || jit_rt_pop(vm, &a) != 0)
            return -1;
        if ((op == CL_OP_JZ && a != 0) || (op == CL_OP_JNZ && a == 0))
            return 0;
        return jump_rel(vm, rel);
    case CL_OP_SYS:
        return jit_rt_sys(vm);
    case CL_OP_HALT:
        vm->state = CLVM_HALTED;
        return 1;
    default:
        return fault(vm, CLVM_FAULT_OPCODE, vm->pc);
    }
}

int jit_rt_exec(ClvmVm *vm, uint8_t op) {
    if (vm == 0)
        return -1;
    return jit_rt_exec_op(vm, op);
}

int jit_rt_exec_at_pc(ClvmVm *vm) {
    uint8_t op;

    if (vm == 0)
        return -1;
    if (vm->pc >= vm->code_size)
        return fault(vm, CLVM_FAULT_PC, vm->pc);
    op = vm->code[vm->pc++];
    return jit_rt_exec_op(vm, op);
}

int jit_rt_run_range(ClvmVm *vm, uint32_t start, uint32_t end) {
    if (vm == 0 || start > end || end > vm->code_size)
        return -1;
    vm->pc = start;
    while (vm->pc < end) {
        int r = jit_rt_exec_at_pc(vm);
        if (r < 0)
            return -1;
        if (r > 0)
            return r;
    }
    return 0;
}
