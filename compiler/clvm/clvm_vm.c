#include "clvm_vm.h"

typedef union {
    uint32_t u;
    float f;
} Fbits;

static void bytes_zero(uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = 0;
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t read_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static int32_t fbinop_bits(uint8_t op, int32_t abits, int32_t bbits) {
    Fbits fa;
    Fbits fb;
    Fbits fr;

    fa.u = (uint32_t)abits;
    fb.u = (uint32_t)bbits;
    if (op == CL_OP_FADD)
        fr.f = fa.f + fb.f;
    else if (op == CL_OP_FSUB)
        fr.f = fa.f - fb.f;
    else if (op == CL_OP_FMUL)
        fr.f = fa.f * fb.f;
    else if (op == CL_OP_FDIV)
        fr.f = fa.f / fb.f;
    else if (op == CL_OP_FEQ)
        return fa.f == fb.f;
    else if (op == CL_OP_FLT)
        return fa.f < fb.f;
    else
        return fa.f <= fb.f;
    return (int32_t)fr.u;
}

static ClvmStepResult fail(ClvmVm *vm, ClvmFault fault, uint32_t pc) {
    vm->fault = fault;
    vm->fault_pc = pc;
    vm->state = CLVM_FAULTED;
    return CLVM_STEP_FAULT;
}

static int fetch(ClvmVm *vm, uint32_t count, const uint8_t **out) {
    if (vm->pc > vm->code_size || count > vm->code_size - vm->pc)
        return 0;
    *out = vm->code + vm->pc;
    vm->pc += count;
    return 1;
}

static int jump_rel(ClvmVm *vm, int16_t rel) {
    int64_t target = (int64_t)vm->pc + rel;
    if (target < 0 || target >= (int64_t)vm->code_size)
        return 0;
    vm->pc = (uint32_t)target;
    return 1;
}

int clvm_vm_push(ClvmVm *vm, int32_t value) {
    if (vm->sp == CLVM_STACK_MAX) return 0;
    vm->stack[vm->sp++] = value;
    return 1;
}

int clvm_vm_pop(ClvmVm *vm, int32_t *value) {
    if (vm->sp == 0) return 0;
    *value = vm->stack[--vm->sp];
    return 1;
}

void clvm_vm_init(ClvmVm *vm, const ClvmImage *image,
                  ClvmSysFn sys, void *sys_user) {
    vm->code = image->code;
    vm->code_size = image->code_size;
    vm->pc = image->entry;
    vm->sp = 0;
    vm->csp = 0;
    bytes_zero(vm->memory, sizeof(vm->memory));
    vm->wake_tick = 0;
    vm->executed = 0;
    vm->state = CLVM_READY;
    vm->fault = CLVM_FAULT_NONE;
    vm->fault_pc = 0;
    vm->sys = sys;
    vm->sys_user = sys_user;
}

void clvm_vm_wait(ClvmVm *vm, uint32_t wake_tick) {
    vm->wake_tick = wake_tick;
    vm->state = CLVM_WAITING;
}

void clvm_vm_wake(ClvmVm *vm, uint32_t now) {
    if (vm->state == CLVM_WAITING &&
        (int32_t)(now - vm->wake_tick) >= 0)
        vm->state = CLVM_READY;
}

const char *clvm_fault_text(ClvmFault fault) {
    static const char *const text[] = {
        "none", "pc outside code", "unknown opcode", "truncated operand",
        "operand stack underflow", "operand stack overflow",
        "call stack underflow", "call stack overflow", "division by zero",
        "signed division overflow", "memory address outside 64 KiB",
        "jump target outside code", "SYS rejected"
    };
    unsigned i = (unsigned)fault;
    if (i >= sizeof(text) / sizeof(text[0])) return "unknown VM fault";
    return text[i];
}

ClvmStepResult clvm_step(ClvmVm *vm, uint32_t budget) {
    uint32_t used;
    if (vm == NULL || vm->code == NULL)
        return fail(vm, CLVM_FAULT_PC, 0);
    if (vm->state == CLVM_WAITING) return CLVM_STEP_YIELD;
    if (vm->state == CLVM_HALTED) return CLVM_STEP_HALT;
    if (vm->state == CLVM_FAULTED) return CLVM_STEP_FAULT;
    vm->state = CLVM_RUNNING;

    for (used = 0; used < budget; ++used) {
        const uint8_t *arg;
        uint32_t op_pc = vm->pc;
        uint8_t op;
        int32_t a, b;
        if (!fetch(vm, 1, &arg))
            return fail(vm, CLVM_FAULT_PC, op_pc);
        op = arg[0];
        ++vm->executed;

        switch (op) {
        case CL_OP_NOP:
            break;
        case CL_OP_PUSH:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push(vm, (int32_t)read_u32(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_ADD: case CL_OP_SUB: case CL_OP_MUL:
        case CL_OP_DIV: case CL_OP_MOD:
        case CL_OP_EQ: case CL_OP_NE: case CL_OP_LT:
        case CL_OP_LE: case CL_OP_GT: case CL_OP_GE:
            if (!clvm_vm_pop(vm, &b) || !clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if ((op == CL_OP_DIV || op == CL_OP_MOD) && b == 0)
                return fail(vm, CLVM_FAULT_DIV_ZERO, op_pc);
            if ((op == CL_OP_DIV || op == CL_OP_MOD) &&
                a == (int32_t)0x80000000u && b == -1)
                return fail(vm, CLVM_FAULT_DIV_OVERFLOW, op_pc);
            if (op == CL_OP_ADD) a = (int32_t)((uint32_t)a + (uint32_t)b);
            else if (op == CL_OP_SUB) a = (int32_t)((uint32_t)a - (uint32_t)b);
            else if (op == CL_OP_MUL) a = (int32_t)((uint32_t)a * (uint32_t)b);
            else if (op == CL_OP_DIV) a /= b;
            else if (op == CL_OP_MOD) a %= b;
            else if (op == CL_OP_EQ) a = a == b;
            else if (op == CL_OP_NE) a = a != b;
            else if (op == CL_OP_LT) a = a < b;
            else if (op == CL_OP_LE) a = a <= b;
            else if (op == CL_OP_GT) a = a > b;
            else a = a >= b;
            if (!clvm_vm_push(vm, a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_NEG:
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push(vm, (int32_t)(0u - (uint32_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_DUP:
            if (vm->sp == 0)
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push(vm, vm->stack[vm->sp - 1]))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_DROP:
        case CL_OP_PRINT:
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            break;
        case CL_OP_SWAP:
            if (!clvm_vm_pop(vm, &b) || !clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push(vm, b) || !clvm_vm_push(vm, a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_LOAD:
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (a < 0 || (uint32_t)a > CLVM_MEMORY_SIZE - 4)
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push(vm, (int32_t)read_u32(vm->memory + (uint32_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_STORE:
            if (!clvm_vm_pop(vm, &a) || !clvm_vm_pop(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (a < 0 || (uint32_t)a > CLVM_MEMORY_SIZE - 4)
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            write_u32(vm->memory + (uint32_t)a, (uint32_t)b);
            break;
        case CL_OP_JMP: case CL_OP_JZ: case CL_OP_JNZ: case CL_OP_CALL: {
            int16_t rel;
            if (!fetch(vm, 2, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            rel = read_i16(arg);
            if (op == CL_OP_JZ || op == CL_OP_JNZ) {
                if (!clvm_vm_pop(vm, &a))
                    return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
                if ((op == CL_OP_JZ && a != 0) ||
                    (op == CL_OP_JNZ && a == 0))
                    break;
            }
            if (op == CL_OP_CALL) {
                if (vm->csp == CLVM_CALL_MAX)
                    return fail(vm, CLVM_FAULT_CALL_OVERFLOW, op_pc);
                vm->calls[vm->csp++] = vm->pc;
            }
            if (!jump_rel(vm, rel))
                return fail(vm, CLVM_FAULT_BAD_JUMP, op_pc);
            break;
        }
        case CL_OP_RET:
            if (vm->csp == 0)
                return fail(vm, CLVM_FAULT_CALL_UNDERFLOW, op_pc);
            vm->pc = vm->calls[--vm->csp];
            break;
        case CL_OP_SYS:
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (vm->sys == NULL || vm->sys(vm, a, vm->sys_user) != 0)
                return fail(vm, CLVM_FAULT_BAD_SYS, op_pc);
            if (vm->state == CLVM_WAITING) return CLVM_STEP_YIELD;
            break;
        case CL_OP_FLOAD:
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (a < 0 || (uint32_t)a > CLVM_MEMORY_SIZE - 4)
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push(vm, (int32_t)read_u32(vm->memory + (uint32_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_FSTORE:
            if (!clvm_vm_pop(vm, &a) || !clvm_vm_pop(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (a < 0 || (uint32_t)a > CLVM_MEMORY_SIZE - 4)
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            write_u32(vm->memory + (uint32_t)a, (uint32_t)b);
            break;
        case CL_OP_FPUSH:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push(vm, (int32_t)read_u32(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_FADD: case CL_OP_FSUB: case CL_OP_FMUL: case CL_OP_FDIV:
        case CL_OP_FEQ: case CL_OP_FLT: case CL_OP_FLE: {
            Fbits fb;
            if (!clvm_vm_pop(vm, &b) || !clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (op == CL_OP_FDIV) {
                fb.u = (uint32_t)b;
                if (fb.f == 0.0f)
                    return fail(vm, CLVM_FAULT_DIV_ZERO, op_pc);
            }
            a = fbinop_bits(op, a, b);
            if (!clvm_vm_push(vm, a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_FNEG: {
            Fbits v;
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.u = (uint32_t)a;
            v.f = -v.f;
            if (!clvm_vm_push(vm, (int32_t)v.u))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_FTOI: {
            Fbits v;
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.u = (uint32_t)a;
            if (!clvm_vm_push(vm, (int32_t)v.f))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_ITOF: {
            Fbits v;
            if (!clvm_vm_pop(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.f = (float)a;
            if (!clvm_vm_push(vm, (int32_t)v.u))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_HALT:
            vm->state = CLVM_HALTED;
            return CLVM_STEP_HALT;
        default:
            return fail(vm, CLVM_FAULT_OPCODE, op_pc);
        }
    }
    vm->state = CLVM_READY;
    return CLVM_STEP_SLICE;
}
