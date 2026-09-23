#include "clvm_vm.h"
#ifdef __freestanding__
#include "proc.h"
#endif

#ifdef __freestanding__
#include "heap.h"
#include "serial.h"
#define CLVM_MEM_ALLOC(n) kmalloc(n)
#define CLVM_MEM_FREE(p) kfree(p)
#elif defined(CHRIS_RISCV)
static uint8_t g_rvpool[2u * 1024u * 1024u];
static size_t g_rvat;
static void *rv_alloc(size_t n) {
    size_t i;
    uint8_t *p;
    n = (n + 15u) & ~(size_t)15u;
    if (g_rvat + n > sizeof(g_rvpool))
        return 0;
    p = g_rvpool + g_rvat;
    g_rvat += n;
    for (i = 0; i < n; ++i)
        p[i] = 0;
    return p;
}
#define CLVM_MEM_ALLOC(n) rv_alloc(n)
#define CLVM_MEM_FREE(p) ((void)(p))
#define serial_puts(s) ((void)0)
#define serial_write_u64(v) ((void)(v))
#else
#include <stdlib.h>
#define CLVM_MEM_ALLOC(n) calloc(1, (size_t)(n))
#define CLVM_MEM_FREE(p) free(p)
#define serial_puts(s) ((void)0)
#define serial_write_u64(v) ((void)(v))
#endif

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

static uint64_t read_u64(const uint8_t *p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

static int16_t read_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t read_i32(const uint8_t *p) {
    return (int32_t)read_u32(p);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static void write_u64(uint8_t *p, uint64_t value) {
    write_u32(p, (uint32_t)value);
    write_u32(p + 4, (uint32_t)(value >> 32));
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

static int jump_rel16(ClvmVm *vm, int16_t rel) {
    int64_t target = (int64_t)vm->pc + rel;
    if (target < 0 || target >= (int64_t)vm->code_size)
        return 0;
    vm->pc = (uint32_t)target;
    return 1;
}

static int jump_rel32(ClvmVm *vm, int32_t rel) {
    int64_t target = (int64_t)vm->pc + rel;
    if (target < 0 || target >= (int64_t)vm->code_size)
        return 0;
    vm->pc = (uint32_t)target;
    return 1;
}

static int mem_ok(ClvmVm *vm, int64_t addr, uint64_t n) {
    if (vm->memory == 0 || addr < 0)
        return 0;
    if ((uint64_t)addr > vm->mem_size)
        return 0;
    if (n > vm->mem_size - (uint64_t)addr)
        return 0;
    return 1;
}

int clvm_vm_push64(ClvmVm *vm, int64_t value) {
    if (vm->sp == CLVM_STACK_MAX) return 0;
    vm->stack[vm->sp++] = value;
    return 1;
}

int clvm_vm_pop64(ClvmVm *vm, int64_t *value) {
    if (vm->sp == 0) return 0;
    *value = vm->stack[--vm->sp];
    return 1;
}

int clvm_vm_push(ClvmVm *vm, int32_t value) {
    return clvm_vm_push64(vm, (int64_t)value);
}

int clvm_vm_pop(ClvmVm *vm, int32_t *value) {
    int64_t v;
    if (!clvm_vm_pop64(vm, &v)) return 0;
    *value = (int32_t)v;
    return 1;
}

void clvm_vm_set_memory(ClvmVm *vm, uint8_t *mem, uint64_t size) {
    if (vm->mem_owned && vm->memory && vm->memory != mem)
        CLVM_MEM_FREE(vm->memory);
    vm->mem_owned = 0;
    vm->memory = mem;
    vm->mem_size = size;
    /* Doom-sized static data (globals+strings) sits well above 64K.
     * Bump-pointer malloc must start past that or it clobbers the pool. */
    if (size >= (16ull * 1024ull * 1024ull))
        vm->heap_off = 1024ull * 1024ull;
    else if (size > 131072ull)
        vm->heap_off = 65536ull;
    else
        vm->heap_off = size / 2ull;
}

void clvm_vm_init(ClvmVm *vm, const ClvmImage *image,
                  ClvmSysFn sys, void *sys_user) {
    vm->code = image->code;
    vm->code_size = image->code_size;
    vm->pc = image->entry;
    vm->sp = 0;
    vm->csp = 0;
    vm->memory = (uint8_t *)CLVM_MEM_ALLOC(CLVM_MEMORY_SIZE);
    vm->mem_owned = vm->memory ? 1 : 0;
    vm->mem_size = vm->memory ? (uint64_t)CLVM_MEMORY_SIZE : 0;
    if (vm->memory)
        bytes_zero(vm->memory, CLVM_MEMORY_SIZE);
    vm->wake_tick = 0;
    vm->executed = 0;
    vm->state = CLVM_READY;
    vm->fault = CLVM_FAULT_NONE;
    vm->fault_pc = 0;
    vm->sys = sys;
    vm->sys_user = sys_user;
    vm->print_n = 0;
    vm->safepoint = 0;
    vm->on_safepoint = 0;
    vm->join_wait = -1;
    {
        int ti;
        for (ti = 0; ti < 16; ++ti)
            vm->tls[ti] = 0;
    }
    if (vm->mem_size >= (16ull * 1024ull * 1024ull))
        vm->heap_off = 1024ull * 1024ull;
    else if (vm->mem_size > 131072ull)
        vm->heap_off = 65536ull;
    else
        vm->heap_off = vm->mem_size / 2ull;
    bytes_zero((uint8_t *)vm->il_loc, sizeof(vm->il_loc));
    bytes_zero((uint8_t *)vm->il_arg, sizeof(vm->il_arg));
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
        "signed division overflow", "memory address outside VM RAM",
        "jump target outside code", "SYS rejected"
    };
    unsigned i = (unsigned)fault;
    if (i >= sizeof(text) / sizeof(text[0])) return "unknown VM fault";
    return text[i];
}

static void note_print(ClvmVm *vm, int64_t v) {
    if (vm->print_n < 8)
        vm->print_ring[vm->print_n++] = v;
    else {
        uint8_t i;
        for (i = 0; i < 7; ++i)
            vm->print_ring[i] = vm->print_ring[i + 1];
        vm->print_ring[7] = v;
    }
}

int clvm_guest_malloc(ClvmVm *vm, uint64_t n, uint64_t *out) {
    uint64_t need;
    uint64_t p;
    if (!vm || !out || n == 0 || n > (1ull << 40))
        return 0;
    need = (n + 8ull + 7ull) & ~7ull;
    p = vm->heap_off;
    if (p + need > vm->mem_size) {
        serial_puts("guest_malloc fail n=");
        serial_write_u64(n);
        serial_puts(" off=");
        serial_write_u64(p);
        serial_puts(" msz=");
        serial_write_u64(vm->mem_size);
        serial_puts("\n");
        return 0;
    }
    write_u64(vm->memory + p, need);
    vm->heap_off = p + need;
    *out = p + 8ull;
    if (n >= (1ull << 20)) {
        serial_puts("guest_malloc ok n=");
        serial_write_u64(n);
        serial_puts(" p=");
        serial_write_u64(*out);
        serial_puts("\n");
    }
    return 1;
}

int clvm_guest_free(ClvmVm *vm, uint64_t p) {
    (void)vm;
    (void)p;
    return 1;
}

int clvm_guest_realloc(ClvmVm *vm, uint64_t p, uint64_t n, uint64_t *out) {
    uint64_t np;
    uint64_t old;
    uint64_t i;
    if (!clvm_guest_malloc(vm, n, &np))
        return 0;
    if (p != 0 && p >= 8ull && p < vm->mem_size) {
        old = read_u64(vm->memory + (p - 8ull));
        if (old > 8ull) {
            old -= 8ull;
            if (old > n)
                old = n;
            for (i = 0; i < old; ++i)
                vm->memory[np + i] = vm->memory[p + i];
        }
    }
    *out = np;
    return 1;
}

int clvm_guest_setjmp(ClvmVm *vm, uint64_t addr) {
    uint8_t *p;
    uint32_t i;
    if (!vm || addr + 16u + (uint32_t)vm->sp * 8u + 8u > vm->mem_size)
        return 0;
    p = vm->memory + addr;
    write_u32(p, vm->pc);
    write_u32(p + 4, vm->sp);
    write_u32(p + 8, vm->csp);
    write_u32(p + 12, 1);
    p += 16;
    for (i = 0; i < vm->sp; ++i) {
        write_u64(p, (uint64_t)vm->stack[i]);
        p += 8;
    }
    for (i = 0; i < vm->csp; ++i) {
        write_u32(p, vm->calls[i]);
        p += 4;
    }
    return 1;
}

int clvm_guest_longjmp(ClvmVm *vm, uint64_t addr, int64_t val) {
    uint8_t *p;
    uint32_t i;
    uint32_t sp;
    uint32_t csp;
    if (!vm || addr + 16u > vm->mem_size)
        return 0;
    p = vm->memory + addr;
    vm->pc = read_u32(p);
    sp = read_u32(p + 4);
    csp = read_u32(p + 8);
    if (sp > CLVM_STACK_MAX || csp > CLVM_CALL_MAX)
        return 0;
    vm->sp = (uint16_t)sp;
    vm->csp = (uint16_t)csp;
    p += 16;
    for (i = 0; i < sp; ++i) {
        vm->stack[i] = (int64_t)read_u64(p);
        p += 8;
    }
    for (i = 0; i < csp; ++i) {
        vm->calls[i] = read_u32(p);
        p += 4;
    }
    if (vm->sp < CLVM_STACK_MAX)
        vm->stack[vm->sp++] = val ? val : 1;
    return 1;
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
        int64_t a, b;
        if (!fetch(vm, 1, &arg))
            return fail(vm, CLVM_FAULT_PC, op_pc);
        op = arg[0];
        ++vm->executed;
#ifdef __freestanding__
        if ((vm->executed & 8191ull) == 0 && proc_slice_due())
            return CLVM_STEP_YIELD;
#endif

        switch (op) {
        case CL_OP_NOP:
            break;
        case CL_OP_SAFEPOINT:
            vm->safepoint = 1;
            if (vm->on_safepoint)
                vm->on_safepoint(vm);
            vm->safepoint = 0;
#ifdef __freestanding__
            if (proc_slice_due())
                return CLVM_STEP_YIELD;
#endif
            break;
        case CL_OP_PUSH:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)read_u32(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_PUSH64:
            if (!fetch(vm, 8, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)read_u64(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_ADD: case CL_OP_SUB: case CL_OP_MUL:
        case CL_OP_DIV: case CL_OP_MOD:
        case CL_OP_EQ: case CL_OP_NE: case CL_OP_LT:
        case CL_OP_LE: case CL_OP_GT: case CL_OP_GE:
        case CL_OP_AND: case CL_OP_OR: case CL_OP_XOR:
        case CL_OP_SHL: case CL_OP_SHR: case CL_OP_SAR:
        case CL_OP_UDIV: case CL_OP_UMOD: case CL_OP_ULT:
            if (!clvm_vm_pop64(vm, &b) || !clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if ((op == CL_OP_DIV || op == CL_OP_MOD ||
                 op == CL_OP_UDIV || op == CL_OP_UMOD) && b == 0)
                return fail(vm, CLVM_FAULT_DIV_ZERO, op_pc);
            if ((op == CL_OP_DIV || op == CL_OP_MOD) &&
                a == (int64_t)((uint64_t)1 << 63) && b == -1)
                return fail(vm, CLVM_FAULT_DIV_OVERFLOW, op_pc);
            if (op == CL_OP_ADD) a = a + b;
            else if (op == CL_OP_SUB) a = a - b;
            else if (op == CL_OP_MUL) a = a * b;
            else if (op == CL_OP_DIV) a = a / b;
            else if (op == CL_OP_MOD) a = a % b;
            else if (op == CL_OP_UDIV)
                a = (int64_t)((uint64_t)a / (uint64_t)b);
            else if (op == CL_OP_UMOD)
                a = (int64_t)((uint64_t)a % (uint64_t)b);
            else if (op == CL_OP_ULT)
                a = ((uint64_t)a < (uint64_t)b);
            else if (op == CL_OP_EQ) a = a == b;
            else if (op == CL_OP_NE) a = a != b;
            else if (op == CL_OP_LT) a = a < b;
            else if (op == CL_OP_LE) a = a <= b;
            else if (op == CL_OP_GT) a = a > b;
            else if (op == CL_OP_GE) a = a >= b;
            else if (op == CL_OP_AND) a = a & b;
            else if (op == CL_OP_OR) a = a | b;
            else if (op == CL_OP_XOR) a = a ^ b;
            else if (op == CL_OP_SHL) a = a << (b & 63);
            else if (op == CL_OP_SHR) a = (int64_t)((uint64_t)a >> (b & 63));
            else a = a >> (b & 63);
            if (!clvm_vm_push64(vm, a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_NEG:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push64(vm, -a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_NOT:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push64(vm, ~a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_DUP:
            if (vm->sp == 0)
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push64(vm, vm->stack[vm->sp - 1]))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_DROP:
            if (vm->sp > 0)
                vm->sp--;
            break;
        case CL_OP_PRINT:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            note_print(vm, a);
            break;
        case CL_OP_SWAP:
            if (!clvm_vm_pop64(vm, &b) || !clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!clvm_vm_push64(vm, b) || !clvm_vm_push64(vm, a))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_LOAD:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 4))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)read_u32(vm->memory + (uint64_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_STORE:
            if (!clvm_vm_pop64(vm, &a) || !clvm_vm_pop64(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 4))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            write_u32(vm->memory + (uint64_t)a, (uint32_t)b);
            break;
        case CL_OP_LOAD64:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 8))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)read_u64(vm->memory + (uint64_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_STORE64:
            if (!clvm_vm_pop64(vm, &a) || !clvm_vm_pop64(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 8))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            write_u64(vm->memory + (uint64_t)a, (uint64_t)b);
            break;
        case CL_OP_JMP: case CL_OP_JZ: case CL_OP_JNZ: case CL_OP_CALL: {
            int16_t rel;
            if (!fetch(vm, 2, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            rel = read_i16(arg);
            if (op == CL_OP_JZ || op == CL_OP_JNZ) {
                if (!clvm_vm_pop64(vm, &a))
                    return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
                if ((op == CL_OP_JZ && a != 0) ||
                    (op == CL_OP_JNZ && a == 0))
                    break;
            }
            if (op == CL_OP_CALL) {
                if (vm->csp == CLVM_CALL_MAX)
                    return fail(vm, CLVM_FAULT_CALL_OVERFLOW, op_pc);
                vm->calls[vm->csp++] = vm->pc;
                vm->safepoint = 1;
            }
            if (!jump_rel16(vm, rel))
                return fail(vm, CLVM_FAULT_BAD_JUMP, op_pc);
            break;
        }
        case CL_OP_JMP32: case CL_OP_JZ32: case CL_OP_JNZ32: case CL_OP_CALL32: {
            int32_t rel;
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            rel = read_i32(arg);
            if (op == CL_OP_JZ32 || op == CL_OP_JNZ32) {
                if (!clvm_vm_pop64(vm, &a))
                    return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
                if ((op == CL_OP_JZ32 && a != 0) ||
                    (op == CL_OP_JNZ32 && a == 0))
                    break;
            }
            if (op == CL_OP_CALL32) {
                if (vm->csp == CLVM_CALL_MAX)
                    return fail(vm, CLVM_FAULT_CALL_OVERFLOW, op_pc);
                vm->calls[vm->csp++] = vm->pc;
                vm->safepoint = 1;
            }
            if (!jump_rel32(vm, rel))
                return fail(vm, CLVM_FAULT_BAD_JUMP, op_pc);
            break;
        }
        case CL_OP_CALLI:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (a < 0 || (uint64_t)a >= vm->code_size)
                return fail(vm, CLVM_FAULT_BAD_JUMP, op_pc);
            if (vm->csp == CLVM_CALL_MAX)
                return fail(vm, CLVM_FAULT_CALL_OVERFLOW, op_pc);
            vm->calls[vm->csp++] = vm->pc;
            vm->pc = (uint32_t)a;
            vm->safepoint = 1;
            break;
        case CL_OP_RET:
            if (vm->csp == 0)
                return fail(vm, CLVM_FAULT_CALL_UNDERFLOW, op_pc);
            vm->pc = vm->calls[--vm->csp];
            break;
        case CL_OP_LOADB:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 1))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)vm->memory[(uint64_t)a]))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_STOREB:
            if (!clvm_vm_pop64(vm, &a) || !clvm_vm_pop64(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 1))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            vm->memory[(uint64_t)a] = (uint8_t)((uint64_t)b & 0xffu);
            break;
        case CL_OP_SYS:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (vm->sys == NULL || vm->sys(vm, (int32_t)a, vm->sys_user) != 0)
                return fail(vm, CLVM_FAULT_BAD_SYS, op_pc);
            vm->safepoint = 1;
            if (vm->state == CLVM_WAITING) return CLVM_STEP_YIELD;
            break;
        case CL_OP_FLOAD:
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 4))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)read_u32(vm->memory + (uint64_t)a)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_FSTORE:
            if (!clvm_vm_pop64(vm, &a) || !clvm_vm_pop64(vm, &b))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            if (!mem_ok(vm, a, 4))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            write_u32(vm->memory + (uint64_t)a, (uint32_t)b);
            break;
        case CL_OP_FPUSH:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)read_u32(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_FADD: case CL_OP_FSUB: case CL_OP_FMUL: case CL_OP_FDIV:
        case CL_OP_FEQ: case CL_OP_FLT: case CL_OP_FLE: {
            Fbits fb;
            int32_t ia, ib, ir;
            if (!clvm_vm_pop64(vm, &b) || !clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            ia = (int32_t)a;
            ib = (int32_t)b;
            if (op == CL_OP_FDIV) {
                fb.u = (uint32_t)ib;
                if (fb.f == 0.0f)
                    return fail(vm, CLVM_FAULT_DIV_ZERO, op_pc);
            }
            ir = fbinop_bits(op, ia, ib);
            if (!clvm_vm_push64(vm, (int64_t)ir))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_FNEG: {
            Fbits v;
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.u = (uint32_t)a;
            v.f = -v.f;
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)v.u))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_FTOI: {
            Fbits v;
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.u = (uint32_t)a;
            if (!clvm_vm_push64(vm, (int64_t)v.f))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_ITOF: {
            Fbits v;
            if (!clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
            v.f = (float)a;
            if (!clvm_vm_push64(vm, (int64_t)(int32_t)v.u))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        }
        case CL_OP_HALT:
            vm->state = CLVM_HALTED;
            return CLVM_STEP_HALT;
        case CL_OP_LDARG:
            if (!fetch(vm, 1, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (arg[0] >= 16)
                return fail(vm, CLVM_FAULT_OPCODE, op_pc);
            if (!clvm_vm_push64(vm, vm->il_arg[arg[0]]))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_LDLOC:
            if (!fetch(vm, 1, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (arg[0] >= 32)
                return fail(vm, CLVM_FAULT_OPCODE, op_pc);
            if (!clvm_vm_push64(vm, vm->il_loc[arg[0]]))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        case CL_OP_STLOC:
            if (!fetch(vm, 1, &arg) || !clvm_vm_pop64(vm, &a))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (arg[0] >= 32)
                return fail(vm, CLVM_FAULT_OPCODE, op_pc);
            vm->il_loc[arg[0]] = a;
            break;
        case CL_OP_NEWOBJ: {
            uint64_t h;
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_guest_malloc(vm, read_u32(arg) ? read_u32(arg) : 16u, &h))
                return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)h))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            vm->safepoint = 1;
            break;
        }
        case CL_OP_LDFLD:
        case CL_OP_STFLD: {
            uint32_t off;
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            off = read_u32(arg);
            if (op == CL_OP_STFLD) {
                if (!clvm_vm_pop64(vm, &b) || !clvm_vm_pop64(vm, &a))
                    return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
                if (!mem_ok(vm, a + (int64_t)off, 8))
                    return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
                write_u64(vm->memory + (uint64_t)a + off, (uint64_t)b);
            } else {
                if (!clvm_vm_pop64(vm, &a))
                    return fail(vm, CLVM_FAULT_STACK_UNDERFLOW, op_pc);
                if (!mem_ok(vm, a + (int64_t)off, 8))
                    return fail(vm, CLVM_FAULT_BAD_ADDRESS, op_pc);
                if (!clvm_vm_push64(vm, (int64_t)read_u64(vm->memory + (uint64_t)a + off)))
                    return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            }
            break;
        }
        case CL_OP_CALLT:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (vm->csp >= CLVM_CALL_MAX)
                return fail(vm, CLVM_FAULT_CALL_OVERFLOW, op_pc);
            vm->calls[vm->csp++] = vm->pc;
            vm->pc = read_u32(arg);
            vm->safepoint = 1;
            break;
        case CL_OP_LDSTR:
            if (!fetch(vm, 4, &arg))
                return fail(vm, CLVM_FAULT_TRUNCATED, op_pc);
            if (!clvm_vm_push64(vm, (int64_t)read_u32(arg)))
                return fail(vm, CLVM_FAULT_STACK_OVERFLOW, op_pc);
            break;
        default:
            return fail(vm, CLVM_FAULT_OPCODE, op_pc);
        }
    }
    vm->state = CLVM_READY;
    return CLVM_STEP_SLICE;
}
