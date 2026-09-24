#include "jit_compile.h"

#include "jit_emit.h"
#include "jit_runtime.h"
#include "gc/gc.h"
#include "serial.h"
#include <stddef.h>

#define JIT_OFF_PC     ((uint32_t)offsetof(ClvmVm, pc))
#define JIT_OFF_STATE  ((uint32_t)offsetof(ClvmVm, state))
#define JIT_OFF_STACK  ((uint32_t)offsetof(ClvmVm, stack))
#define JIT_OFF_SP     ((uint32_t)offsetof(ClvmVm, sp))
#define JIT_OFF_CODE   ((uint32_t)offsetof(ClvmVm, code))
#define JIT_OFF_MEM    ((uint32_t)offsetof(ClvmVm, memory))
#define JIT_OFF_MSZ    ((uint32_t)offsetof(ClvmVm, mem_size))
#define JIT_OFF_CALLS  ((uint32_t)offsetof(ClvmVm, calls))
#define JIT_OFF_CSP    ((uint32_t)offsetof(ClvmVm, csp))
#define JIT_OFF_ONSP   ((uint32_t)offsetof(ClvmVm, on_safepoint))
#define JIT_OFF_FAULT  ((uint32_t)offsetof(ClvmVm, fault))
#define JIT_OFF_FPC    ((uint32_t)offsetof(ClvmVm, fault_pc))
#define REG_VM   3
#define REG_ARG0 7
#define JIT_MAX_PCS (1024u * 1024u)
#define JIT_MAX_PATCH 131072u

extern ClvmStepResult clvm_step(ClvmVm *vm, uint32_t budget);

static uint32_t g_nat[JIT_MAX_PCS];
static uint32_t g_psite[JIT_MAX_PATCH];
static uint32_t g_ptgt[JIT_MAX_PATCH];
static uint32_t g_npatch;
static uint32_t g_fault_ba;
static uint32_t g_call_ovf;

static int insn_len(const uint8_t *code, uint32_t pc, uint32_t size) {
    uint8_t op;

    if (pc >= size)
        return 0;
    op = code[pc];
    switch (op) {
    case CL_OP_PUSH:
    case CL_OP_FPUSH:
        return 5;
    case CL_OP_JMP:
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_CALL:
        return 3;
    case CL_OP_LDARG:
    case CL_OP_STLOC:
    case CL_OP_LDLOC:
        return 2;
    case CL_OP_NEWOBJ:
    case CL_OP_LDFLD:
    case CL_OP_STFLD:
    case CL_OP_CALLT:
    case CL_OP_LDSTR:
    case CL_OP_JMP32:
    case CL_OP_JZ32:
    case CL_OP_JNZ32:
    case CL_OP_CALL32:
        return 5;
    case CL_OP_PUSH64:
        return 9;
    default:
        return 1;
    }
}

static int32_t rd_i32(const uint8_t *c, uint32_t pc) {
    return (int32_t)((uint32_t)c[pc] | ((uint32_t)c[pc + 1u] << 8) |
                     ((uint32_t)c[pc + 2u] << 16) | ((uint32_t)c[pc + 3u] << 24));
}

static int16_t rd_i16(const uint8_t *c, uint32_t pc) {
    return (int16_t)((uint16_t)c[pc] | ((uint16_t)c[pc + 1u] << 8));
}

static int emit_jmp_to(JitBuf *j, uint32_t target) {
    int32_t rel = (int32_t)target - (int32_t)(j->used + 5u);
    return jit_emit_jmp_rel32(j, rel);
}

static int emit_je_to(JitBuf *j, uint32_t target) {
    int32_t rel = (int32_t)target - (int32_t)(j->used + 6u);
    return jit_emit_je_rel32(j, rel);
}

static int emit_jge_to(JitBuf *j, uint32_t target) {
    int32_t rel = (int32_t)target - (int32_t)(j->used + 6u);
    return jit_emit_jge_rel32(j, rel);
}

static int emit_jb_to(JitBuf *j, uint32_t target) {
    uint8_t b[2] = { 0x0F, 0x82 };
    int32_t rel;
    if (jit_emit(j, b, 2) != 0)
        return -1;
    rel = (int32_t)target - (int32_t)(j->used + 4u);
    return jit_emit_u32(j, (uint32_t)rel);
}

static int emit_ja_to(JitBuf *j, uint32_t target) {
    uint8_t b[2] = { 0x0F, 0x87 };
    int32_t rel;
    if (jit_emit(j, b, 2) != 0)
        return -1;
    rel = (int32_t)target - (int32_t)(j->used + 4u);
    return jit_emit_u32(j, (uint32_t)rel);
}

static int emit_js_to(JitBuf *j, uint32_t target) {
    uint8_t b[2] = { 0x0F, 0x88 };
    int32_t rel;

    if (jit_emit(j, b, 2) != 0)
        return -1;
    rel = (int32_t)target - (int32_t)(j->used + 4u);
    return jit_emit_u32(j, (uint32_t)rel);
}

static int emit_set_pc(JitBuf *j, uint32_t pc) {
    uint8_t b[2] = { 0xC7, 0x83 };
    return jit_emit(j, b, 2) != 0 || jit_emit_u32(j, JIT_OFF_PC) != 0 ||
           jit_emit_u32(j, pc) != 0;
}

static int emit_set_state(JitBuf *j, uint32_t st) {
    uint8_t b[2] = { 0xC7, 0x83 };
    return jit_emit(j, b, 2) != 0 || jit_emit_u32(j, JIT_OFF_STATE) != 0 ||
           jit_emit_u32(j, st) != 0;
}

static int emit_load_sp(JitBuf *j) {
    uint8_t b[3] = { 0x0F, 0xB7, 0x8B };
    return jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_SP) != 0;
}

static int emit_store_sp(JitBuf *j) {
    uint8_t b[3] = { 0x66, 0x89, 0x8B };
    return jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_SP) != 0;
}

static int emit_stk_rax(JitBuf *j, int store) {
    uint8_t b[4];
    b[0] = 0x48;
    b[1] = (uint8_t)(store ? 0x89 : 0x8B);
    b[2] = 0x84;
    b[3] = 0xCB;
    return jit_emit(j, b, 4) != 0 || jit_emit_u32(j, JIT_OFF_STACK) != 0;
}

static int emit_vm_epilogue(JitBuf *j) {
    static const uint8_t pop_rbx[] = { 0x5B };
    return jit_emit(j, pop_rbx, 1) != 0 || jit_emit_epilogue(j) != 0;
}

static int emit_ret_value(JitBuf *j, ClvmStepResult value) {
    return jit_emit_mov_r32_imm(j, 0, (int32_t)value) != 0 ||
           emit_vm_epilogue(j) != 0;
}

static int emit_pop_rax(JitBuf *j, uint32_t fault) {
    uint8_t test[] = { 0x85, 0xC9 };
    uint8_t dec[] = { 0xFF, 0xC9 };
    if (emit_load_sp(j) != 0 || jit_emit(j, test, 2) != 0 ||
        emit_je_to(j, fault) != 0 || jit_emit(j, dec, 2) != 0 ||
        emit_store_sp(j) != 0 || emit_stk_rax(j, 0) != 0)
        return -1;
    return 0;
}

static int emit_pop_rdx(JitBuf *j, uint32_t fault) {
    uint8_t test[] = { 0x85, 0xC9 };
    uint8_t dec[] = { 0xFF, 0xC9 };
    uint8_t ld[] = { 0x48, 0x8B, 0x94, 0xCB };
    if (emit_load_sp(j) != 0 || jit_emit(j, test, 2) != 0 ||
        emit_je_to(j, fault) != 0 || jit_emit(j, dec, 2) != 0 ||
        emit_store_sp(j) != 0 || jit_emit(j, ld, 4) != 0 ||
        jit_emit_u32(j, JIT_OFF_STACK) != 0)
        return -1;
    return 0;
}

static int emit_push_rax(JitBuf *j, uint32_t fault) {
    uint8_t cmp[] = { 0x81, 0xF9 };
    uint8_t inc[] = { 0xFF, 0xC1 };
    if (emit_load_sp(j) != 0 || jit_emit(j, cmp, 2) != 0 ||
        jit_emit_u32(j, CLVM_STACK_MAX) != 0 || emit_jge_to(j, fault) != 0 ||
        emit_stk_rax(j, 1) != 0 || jit_emit(j, inc, 2) != 0 ||
        emit_store_sp(j) != 0)
        return -1;
    return 0;
}

static int emit_jmp_pc(JitBuf *j, uint32_t target_pc) {
    if (g_npatch >= JIT_MAX_PATCH)
        return -1;
    if (jit_emit_jmp_rel32(j, 0) != 0)
        return -1;
    g_psite[g_npatch] = j->used - 4u;
    g_ptgt[g_npatch] = target_pc;
    g_npatch++;
    return 0;
}

static int emit_jcc_pc(JitBuf *j, int is_jz, uint32_t target_pc) {
    uint8_t b[2] = { 0x0F, (uint8_t)(is_jz ? 0x84 : 0x85) };
    if (g_npatch >= JIT_MAX_PATCH)
        return -1;
    if (jit_emit(j, b, 2) != 0 || jit_emit_u32(j, 0) != 0)
        return -1;
    g_psite[g_npatch] = j->used - 4u;
    g_ptgt[g_npatch] = target_pc;
    g_npatch++;
    return 0;
}

static int emit_helper(JitBuf *j, uint32_t pc, uint32_t after_helper) {
    /* rbx holds the ClvmVm*; SysV says callees preserve it, but our
     * freestanding helpers/syscalls have historically clobbered it.
     * After the function prologue RSP is 16-aligned; one push would leave
     * it 8-mod-16 and the helper's SSE (fillrgb/text) would #GP(0). */
    static const uint8_t push_rbx[] = { 0x53 };
    static const uint8_t pop_rbx[] = { 0x5B };
    static const uint8_t sub8[] = { 0x48, 0x83, 0xEC, 0x08 };
    static const uint8_t add8[] = { 0x48, 0x83, 0xC4, 0x08 };
    if (emit_set_pc(j, pc) != 0)
        return -1;
    if (jit_emit(j, push_rbx, 1) != 0)
        return -1;
    if (jit_emit(j, sub8, 4) != 0)
        return -1;
    if (jit_emit_mov_r64_r64(j, REG_ARG0, REG_VM) != 0)
        return -1;
    if (jit_emit_call_r64(j, (uint64_t)(uintptr_t)jit_rt_exec_at_pc) != 0)
        return -1;
    if (jit_emit(j, add8, 4) != 0)
        return -1;
    if (jit_emit(j, pop_rbx, 1) != 0)
        return -1;
    return emit_jmp_to(j, after_helper);
}

static int emit_binop64(JitBuf *j, uint32_t fault, uint8_t opb0, uint8_t opb1,
                        uint8_t opb2, int n) {
    uint8_t op[3];
    op[0] = opb0;
    op[1] = opb1;
    op[2] = opb2;
    if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
        return -1;
    if (jit_emit(j, op, (uint32_t)n) != 0)
        return -1;
    return emit_push_rax(j, fault);
}

static int emit_cmp64(JitBuf *j, uint32_t fault, uint8_t setcc) {
    uint8_t cmp[] = { 0x48, 0x39, 0xD0 };
    uint8_t set[] = { 0x0F, 0x00, 0xC0 };
    uint8_t zx[] = { 0x48, 0x0F, 0xB6, 0xC0 };
    set[1] = setcc;
    if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
        return -1;
    if (jit_emit(j, cmp, 3) != 0 || jit_emit(j, set, 3) != 0 ||
        jit_emit(j, zx, 4) != 0)
        return -1;
    return emit_push_rax(j, fault);
}

static int emit_mem(JitBuf *j, int dst_rdx) {
    uint8_t b[3] = { 0x48, 0x8B, (uint8_t)(dst_rdx ? 0x93 : 0x83) };
    return jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_MEM) != 0;
}

static int emit_set_fault(JitBuf *j, ClvmFault f) {
    uint8_t b[2] = { 0xC7, 0x83 };
    return jit_emit(j, b, 2) != 0 || jit_emit_u32(j, JIT_OFF_FAULT) != 0 ||
           jit_emit_u32(j, (uint32_t)f) != 0;
}

static int emit_set_fault_pc_eax(JitBuf *j) {
    uint8_t b[2] = { 0x89, 0x83 };
    return jit_emit(j, b, 2) != 0 || jit_emit_u32(j, JIT_OFF_FPC) != 0;
}

static int emit_bounds(JitBuf *j, uint32_t extra, uint32_t fault) {
    /* Match clvm_vm mem_ok: reject addr < 0 and addr + extra > mem_size.
     * Guest pointers are 32-bit heap offsets; clear any junk in the high
     * half so a STORE64/LOAD64 that only wrote 32 bits still works. */
    uint8_t zext[] = { 0x89, 0xC0 }; /* mov eax, eax */
    uint8_t ld[] = { 0x48, 0x8B, 0x8B };
    uint8_t test[] = { 0x48, 0x85, 0xC0 };
    uint8_t sub[] = { 0x48, 0x81, 0xE9 };
    uint8_t cmp[] = { 0x48, 0x39, 0xC8 };
    if (jit_emit(j, zext, 2) != 0)
        return -1;
    if (jit_emit(j, test, 3) != 0 || emit_js_to(j, fault) != 0)
        return -1;
    if (jit_emit(j, ld, 3) != 0 || jit_emit_u32(j, JIT_OFF_MSZ) != 0)
        return -1;
    if (jit_emit(j, sub, 3) != 0 || jit_emit_u32(j, extra) != 0)
        return -1;
    if (emit_jb_to(j, fault) != 0)
        return -1;
    if (jit_emit(j, cmp, 3) != 0)
        return -1;
    return emit_ja_to(j, fault);
}

static int native_op(uint8_t op) {
    switch (op) {
    case CL_OP_NOP:
    case CL_OP_PUSH:
    case CL_OP_PUSH64:
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
    case CL_OP_NEG:
    case CL_OP_NOT:
    case CL_OP_DUP:
    case CL_OP_DROP:
    case CL_OP_SWAP:
    case CL_OP_LOAD:
    case CL_OP_STORE:
    case CL_OP_LOADB:
    case CL_OP_STOREB:
    case CL_OP_LOAD64:
    case CL_OP_STORE64:
    case CL_OP_FLOAD:
    case CL_OP_FSTORE:
    case CL_OP_FPUSH:
    case CL_OP_JMP:
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_JMP32:
    case CL_OP_JZ32:
    case CL_OP_JNZ32:
    case CL_OP_CALL:
    case CL_OP_CALL32:
    case CL_OP_CALLI:
    case CL_OP_RET:
    case CL_OP_HALT:
    case CL_OP_SAFEPOINT:
    case CL_OP_FADD:
    case CL_OP_FSUB:
    case CL_OP_FMUL:
    case CL_OP_FDIV:
    case CL_OP_ITOF:
    case CL_OP_FTOI:
    case CL_OP_UDIV:
    case CL_OP_UMOD:
    case CL_OP_ULT:
    case CL_OP_ULE:
    case CL_OP_UGT:
    case CL_OP_UGE:
        return 1;
    default:
        return 0;
    }
}

static int emit_insn(JitBuf *j, const uint8_t *code, uint32_t pc, uint32_t size,
                     uint32_t fault, uint32_t slice, uint32_t halt,
                     uint32_t dispatch, uint32_t after_helper) {
    uint8_t op = code[pc];
    int n = insn_len(code, pc, size);
    uint32_t next = pc + (uint32_t)n;
    uint32_t tgt;
    uint8_t b[8];

    if (n <= 0)
        return -1;
    if (!native_op(op))
        return emit_helper(j, pc, after_helper);

    /* Keep PC fresh so fault stubs report the real opcode (linear native
     * code otherwise leaves vm->pc stale across long fall-through runs). */
    if (emit_set_pc(j, pc) != 0)
        return -1;

    switch (op) {
    case CL_OP_NOP:
        return 0;
    case CL_OP_SAFEPOINT: {
        uint8_t ld[] = { 0x48, 0x8B, 0x83 };
        uint8_t test[] = { 0x48, 0x85, 0xC0 };
        uint8_t jz[] = { 0x74, 0x17 }; /* skip aligned call stub (23 bytes) */
        uint8_t push_rbx[] = { 0x53 };
        uint8_t pushd[] = { 0x57, 0x56, 0x51, 0x52 };
        uint8_t sub8[] = { 0x48, 0x83, 0xEC, 0x08 };
        uint8_t add8[] = { 0x48, 0x83, 0xC4, 0x08 };
        uint8_t mov[] = { 0x48, 0x89, 0xDF };
        uint8_t call[] = { 0xFF, 0xD0 };
        uint8_t popd[] = { 0x5A, 0x59, 0x5E, 0x5F };
        uint8_t pop_rbx[] = { 0x5B };
        uint8_t set[] = { 0xC6, 0x83 };
        if (jit_emit(j, set, 2) != 0 ||
            jit_emit_u32(j, (uint32_t)offsetof(ClvmVm, safepoint)) != 0)
            return -1;
        {
            uint8_t one = 1;
            if (jit_emit(j, &one, 1) != 0)
                return -1;
        }
        if (jit_emit(j, ld, 3) != 0 || jit_emit_u32(j, JIT_OFF_ONSP) != 0 ||
            jit_emit(j, test, 3) != 0 || jit_emit(j, jz, 2) != 0 ||
            jit_emit(j, push_rbx, 1) != 0 ||
            jit_emit(j, pushd, 4) != 0 || jit_emit(j, sub8, 4) != 0 ||
            jit_emit(j, mov, 3) != 0 ||
            jit_emit(j, call, 2) != 0 || jit_emit(j, add8, 4) != 0 ||
            jit_emit(j, popd, 4) != 0 ||
            jit_emit(j, pop_rbx, 1) != 0)
            return -1;
        if (jit_emit(j, set, 2) != 0 ||
            jit_emit_u32(j, (uint32_t)offsetof(ClvmVm, safepoint)) != 0)
            return -1;
        {
            uint8_t z = 0;
            if (jit_emit(j, &z, 1) != 0)
                return -1;
        }
        return 0;
    }
    case CL_OP_HALT:
        return emit_set_state(j, CLVM_HALTED) != 0 || emit_jmp_to(j, halt) != 0 ?
               -1 : 0;
    case CL_OP_PUSH:
    case CL_OP_FPUSH:
        if (jit_emit_mov_r32_imm(j, 0, rd_i32(code, pc + 1u)) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x63; b[2] = 0xC0;
        if (jit_emit(j, b, 3) != 0)
            return -1;
        return emit_push_rax(j, fault);
    case CL_OP_PUSH64: {
        uint64_t v = (uint32_t)rd_i32(code, pc + 1u);
        v |= ((uint64_t)(uint32_t)rd_i32(code, pc + 5u)) << 32;
        if (jit_emit_mov_r64_imm(j, 0, v) != 0)
            return -1;
        return emit_push_rax(j, fault);
    }
    case CL_OP_ADD:
        return emit_binop64(j, fault, 0x48, 0x01, 0xD0, 3);
    case CL_OP_SUB:
        return emit_binop64(j, fault, 0x48, 0x29, 0xD0, 3);
    case CL_OP_MUL:
        if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x0F; b[2] = 0xAF; b[3] = 0xC2;
        return jit_emit(j, b, 4) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_FADD:
    case CL_OP_FSUB:
    case CL_OP_FMUL:
    case CL_OP_FDIV: {
        uint8_t md0[] = { 0x66, 0x0F, 0x6E, 0xC0 };
        uint8_t md1[] = { 0x66, 0x0F, 0x6E, 0xCA };
        uint8_t ss[4];
        uint8_t back[] = { 0x66, 0x0F, 0x7E, 0xC0 };
        ss[0] = 0xF3;
        ss[1] = 0x0F;
        ss[3] = 0xC1;
        ss[2] = (uint8_t)(op == CL_OP_FADD ? 0x58 :
                          (op == CL_OP_FSUB ? 0x5C :
                           (op == CL_OP_FMUL ? 0x59 : 0x5E)));
        if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
            return -1;
        if (jit_emit(j, md0, 4) != 0 || jit_emit(j, md1, 4) != 0 ||
            jit_emit(j, ss, 4) != 0 || jit_emit(j, back, 4) != 0)
            return -1;
        return emit_push_rax(j, fault);
    }
    case CL_OP_ITOF: {
        uint8_t cvt[] = { 0xF3, 0x48, 0x0F, 0x2A, 0xC0 };
        uint8_t back[] = { 0x66, 0x0F, 0x7E, 0xC0 };
        if (emit_pop_rax(j, fault) != 0)
            return -1;
        return jit_emit(j, cvt, 5) != 0 || jit_emit(j, back, 4) != 0 ||
               emit_push_rax(j, fault) != 0 ? -1 : 0;
    }
    case CL_OP_FTOI: {
        uint8_t md[] = { 0x66, 0x0F, 0x6E, 0xC0 };
        uint8_t cvt[] = { 0xF3, 0x48, 0x0F, 0x2C, 0xC0 };
        if (emit_pop_rax(j, fault) != 0)
            return -1;
        return jit_emit(j, md, 4) != 0 || jit_emit(j, cvt, 5) != 0 ||
               emit_push_rax(j, fault) != 0 ? -1 : 0;
    }
    case CL_OP_AND:
        return emit_binop64(j, fault, 0x48, 0x21, 0xD0, 3);
    case CL_OP_OR:
        return emit_binop64(j, fault, 0x48, 0x09, 0xD0, 3);
    case CL_OP_XOR:
        return emit_binop64(j, fault, 0x48, 0x31, 0xD0, 3);
    case CL_OP_EQ:
        return emit_cmp64(j, fault, 0x94);
    case CL_OP_NE:
        return emit_cmp64(j, fault, 0x95);
    case CL_OP_LT:
        return emit_cmp64(j, fault, 0x9C);
    case CL_OP_LE:
        return emit_cmp64(j, fault, 0x9E);
    case CL_OP_GT:
        return emit_cmp64(j, fault, 0x9F);
    case CL_OP_GE:
        return emit_cmp64(j, fault, 0x9D);
    case CL_OP_NEG:
        if (emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0xF7; b[2] = 0xD8;
        return jit_emit(j, b, 3) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_NOT:
        if (emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0xF7; b[2] = 0xD0;
        return jit_emit(j, b, 3) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_DROP:
        /* Discard TOS if present; empty DROP is a no-op. */
        {
            uint8_t test[] = { 0x85, 0xC9 };
            uint8_t dec[] = { 0xFF, 0xC9 };
            uint32_t skip;
            if (emit_load_sp(j) != 0 || jit_emit(j, test, 2) != 0)
                return -1;
            if (jit_emit_je_rel32(j, 0) != 0)
                return -1;
            skip = j->used - 4u;
            if (jit_emit(j, dec, 2) != 0 || emit_store_sp(j) != 0)
                return -1;
            jit_patch_rel32(j, skip, j->used);
            return 0;
        }
    case CL_OP_DUP:
        if (emit_pop_rax(j, fault) != 0 || emit_push_rax(j, fault) != 0)
            return -1;
        return emit_push_rax(j, fault);
    case CL_OP_SWAP:
        if (emit_pop_rax(j, fault) != 0 || emit_pop_rdx(j, fault) != 0)
            return -1;
        if (emit_push_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x89; b[2] = 0xD0;
        return jit_emit(j, b, 3) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_SHL:
    case CL_OP_SHR:
    case CL_OP_SAR:
        if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x89; b[2] = 0xD1;
        if (jit_emit(j, b, 3) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0xD3;
        b[2] = (uint8_t)(op == CL_OP_SHL ? 0xE0 : (op == CL_OP_SHR ? 0xE8 : 0xF8));
        return jit_emit(j, b, 3) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_DIV:
    case CL_OP_MOD: {
        /* Divisor must live in rcx: emit_load_sp/clobber ecx, and cqo
         * overwrites rdx — so pop both first, then mov rcx, rdx.
         * Divisor 0: push 0 (soft fail) instead of UNDERFLOW stub. */
        uint8_t test[] = { 0x48, 0x85, 0xC9 };
        uint8_t cqo[] = { 0x48, 0x99 };
        uint8_t idiv[] = { 0x48, 0xF7, 0xF9 };
        uint8_t mov_d[] = { 0x48, 0x89, 0xD0 };
        uint8_t xor_a[] = { 0x48, 0x31, 0xC0 };
        uint32_t jz_site;
        uint32_t end_site;
        if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x89; b[2] = 0xD1; /* mov rcx, rdx */
        if (jit_emit(j, b, 3) != 0 || jit_emit(j, test, 3) != 0)
            return -1;
        if (jit_emit_je_rel32(j, 0) != 0)
            return -1;
        jz_site = j->used - 4u;
        if (jit_emit(j, cqo, 2) != 0 || jit_emit(j, idiv, 3) != 0)
            return -1;
        if (op == CL_OP_MOD && jit_emit(j, mov_d, 3) != 0)
            return -1;
        if (jit_emit_jmp_rel32(j, 0) != 0)
            return -1;
        end_site = j->used - 4u;
        jit_patch_rel32(j, jz_site, j->used);
        if (jit_emit(j, xor_a, 3) != 0)
            return -1;
        jit_patch_rel32(j, end_site, j->used);
        return emit_push_rax(j, fault);
    }
    case CL_OP_UDIV:
    case CL_OP_UMOD: {
        uint8_t test[] = { 0x48, 0x85, 0xC9 };
        uint8_t xor_d[] = { 0x48, 0x31, 0xD2 };
        uint8_t div[] = { 0x48, 0xF7, 0xF1 };
        uint8_t mov_d[] = { 0x48, 0x89, 0xD0 };
        if (emit_pop_rdx(j, fault) != 0 || emit_pop_rax(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x89; b[2] = 0xD1; /* mov rcx, rdx */
        if (jit_emit(j, b, 3) != 0 ||
            jit_emit(j, test, 3) != 0 || emit_je_to(j, fault) != 0 ||
            jit_emit(j, xor_d, 3) != 0 || jit_emit(j, div, 3) != 0)
            return -1;
        if (op == CL_OP_UMOD && jit_emit(j, mov_d, 3) != 0)
            return -1;
        return emit_push_rax(j, fault);
    }
    case CL_OP_ULT:
        return emit_cmp64(j, fault, 0x92); /* setb */
    case CL_OP_ULE:
        return emit_cmp64(j, fault, 0x96); /* setbe */
    case CL_OP_UGT:
        return emit_cmp64(j, fault, 0x97); /* seta */
    case CL_OP_UGE:
        return emit_cmp64(j, fault, 0x93); /* setae */
    case CL_OP_LOAD:
    case CL_OP_FLOAD:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 4, g_fault_ba) != 0 ||
            emit_mem(j, 1) != 0)
            return -1;
        b[0] = 0x8B; b[1] = 0x04; b[2] = 0x02;
        if (jit_emit(j, b, 3) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x63; b[2] = 0xC0;
        return jit_emit(j, b, 3) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_LOAD64:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 8, g_fault_ba) != 0 ||
            emit_mem(j, 1) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x8B; b[2] = 0x04; b[3] = 0x02;
        return jit_emit(j, b, 4) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_LOADB:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 1, g_fault_ba) != 0 ||
            emit_mem(j, 1) != 0)
            return -1;
        b[0] = 0x0F; b[1] = 0xB6; b[2] = 0x04; b[3] = 0x02;
        return jit_emit(j, b, 4) != 0 || emit_push_rax(j, fault) != 0 ? -1 : 0;
    case CL_OP_STORE:
    case CL_OP_FSTORE:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 4, g_fault_ba) != 0 ||
            emit_pop_rdx(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x8B; b[2] = 0x8B;
        if (jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_MEM) != 0)
            return -1;
        b[0] = 0x89; b[1] = 0x14; b[2] = 0x01;
        return jit_emit(j, b, 3);
    case CL_OP_STORE64:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 8, g_fault_ba) != 0 ||
            emit_pop_rdx(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x8B; b[2] = 0x8B;
        if (jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_MEM) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x89; b[2] = 0x14; b[3] = 0x01;
        return jit_emit(j, b, 4);
    case CL_OP_STOREB:
        if (emit_pop_rax(j, fault) != 0 || emit_bounds(j, 1, g_fault_ba) != 0 ||
            emit_pop_rdx(j, fault) != 0)
            return -1;
        b[0] = 0x48; b[1] = 0x8B; b[2] = 0x8B;
        if (jit_emit(j, b, 3) != 0 || jit_emit_u32(j, JIT_OFF_MEM) != 0)
            return -1;
        b[0] = 0x88; b[1] = 0x14; b[2] = 0x01;
        return jit_emit(j, b, 3);
    case CL_OP_JMP:
    case CL_OP_JMP32:
        tgt = next + (op == CL_OP_JMP
                          ? (uint32_t)(int32_t)rd_i16(code, pc + 1u)
                          : (uint32_t)rd_i32(code, pc + 1u));
        if (tgt < pc) {
            uint8_t dec[] = { 0xFF, 0x4D, 0xF0 };
            uint8_t cmp0[] = { 0x83, 0x7D, 0xF0, 0x00 };
            if (jit_emit(j, dec, 3) != 0 || jit_emit(j, cmp0, 4) != 0)
                return -1;
            if (jit_emit_jne_rel32(j, 0) != 0)
                return -1;
            {
                uint32_t site = j->used - 4u;
                if (emit_set_pc(j, tgt) != 0 || emit_jmp_to(j, slice) != 0)
                    return -1;
                jit_patch_rel32(j, site, j->used);
            }
        }
        return emit_jmp_pc(j, tgt);
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_JZ32:
    case CL_OP_JNZ32: {
        int is_jz = (op == CL_OP_JZ || op == CL_OP_JZ32);
        uint8_t test[] = { 0x48, 0x85, 0xC0 };
        tgt = next + ((op == CL_OP_JZ || op == CL_OP_JNZ)
                          ? (uint32_t)(int32_t)rd_i16(code, pc + 1u)
                          : (uint32_t)rd_i32(code, pc + 1u));
        if (emit_pop_rax(j, fault) != 0 || jit_emit(j, test, 3) != 0)
            return -1;
        return emit_jcc_pc(j, is_jz, tgt);
    }
    case CL_OP_CALL:
    case CL_OP_CALL32: {
        uint8_t ld[] = { 0x0F, 0xB7, 0x8B };
        uint8_t cmp[] = { 0x81, 0xF9 };
        uint8_t st[] = { 0x89, 0x84, 0x8B };
        uint8_t inc[] = { 0xFF, 0xC1 };
        uint8_t sv[] = { 0x66, 0x89, 0x8B };
        tgt = next + (op == CL_OP_CALL
                          ? (uint32_t)(int32_t)rd_i16(code, pc + 1u)
                          : (uint32_t)rd_i32(code, pc + 1u));
        if (jit_emit(j, ld, 3) != 0 || jit_emit_u32(j, JIT_OFF_CSP) != 0 ||
            jit_emit(j, cmp, 2) != 0 || jit_emit_u32(j, CLVM_CALL_MAX) != 0 ||
            emit_jge_to(j, g_call_ovf) != 0 ||
            jit_emit_mov_r32_imm(j, 0, (int32_t)next) != 0 ||
            jit_emit(j, st, 3) != 0 || jit_emit_u32(j, JIT_OFF_CALLS) != 0 ||
            jit_emit(j, inc, 2) != 0 || jit_emit(j, sv, 3) != 0 ||
            jit_emit_u32(j, JIT_OFF_CSP) != 0)
            return -1;
        return emit_jmp_pc(j, tgt);
    }
    case CL_OP_CALLI: {
        /* Indirect call: pop absolute bytecode PC, push return, dispatch. */
        uint8_t ld[] = { 0x0F, 0xB7, 0x8B };
        uint8_t cmp[] = { 0x81, 0xF9 };
        uint8_t st[] = { 0x89, 0x84, 0x8B };
        uint8_t inc[] = { 0xFF, 0xC1 };
        uint8_t sv[] = { 0x66, 0x89, 0x8B };
        uint8_t stpc[] = { 0x89, 0x83 };
        uint8_t cmpa[] = { 0x3D };
        if (emit_pop_rax(j, fault) != 0)
            return -1;
        if (jit_emit(j, cmpa, 1) != 0 || jit_emit_u32(j, size) != 0 ||
            emit_jge_to(j, fault) != 0)
            return -1;
        /* save target in rdx while we touch call stack with ecx */
        b[0] = 0x48; b[1] = 0x89; b[2] = 0xC2; /* mov rdx, rax */
        if (jit_emit(j, b, 3) != 0)
            return -1;
        if (jit_emit(j, ld, 3) != 0 || jit_emit_u32(j, JIT_OFF_CSP) != 0 ||
            jit_emit(j, cmp, 2) != 0 || jit_emit_u32(j, CLVM_CALL_MAX) != 0 ||
            emit_jge_to(j, g_call_ovf) != 0 ||
            jit_emit_mov_r32_imm(j, 0, (int32_t)next) != 0 ||
            jit_emit(j, st, 3) != 0 || jit_emit_u32(j, JIT_OFF_CALLS) != 0 ||
            jit_emit(j, inc, 2) != 0 || jit_emit(j, sv, 3) != 0 ||
            jit_emit_u32(j, JIT_OFF_CSP) != 0)
            return -1;
        b[0] = 0x89; b[1] = 0xD0; /* mov eax, edx */
        if (jit_emit(j, b, 2) != 0 ||
            jit_emit(j, stpc, 2) != 0 || jit_emit_u32(j, JIT_OFF_PC) != 0)
            return -1;
        return emit_jmp_to(j, dispatch);
    }
    case CL_OP_RET: {
        uint8_t ld[] = { 0x0F, 0xB7, 0x8B };
        uint8_t test[] = { 0x85, 0xC9 };
        uint8_t dec[] = { 0xFF, 0xC9 };
        uint8_t sv[] = { 0x66, 0x89, 0x8B };
        uint8_t load[] = { 0x8B, 0x84, 0x8B };
        uint8_t stpc[] = { 0x89, 0x83 };
        if (jit_emit(j, ld, 3) != 0 || jit_emit_u32(j, JIT_OFF_CSP) != 0 ||
            jit_emit(j, test, 2) != 0 || emit_je_to(j, fault) != 0 ||
            jit_emit(j, dec, 2) != 0 || jit_emit(j, sv, 3) != 0 ||
            jit_emit_u32(j, JIT_OFF_CSP) != 0 ||
            jit_emit(j, load, 3) != 0 || jit_emit_u32(j, JIT_OFF_CALLS) != 0 ||
            jit_emit(j, stpc, 2) != 0 || jit_emit_u32(j, JIT_OFF_PC) != 0)
            return -1;
        return emit_jmp_to(j, dispatch);
    }
    default:
        return emit_helper(j, pc, after_helper);
    }
}

static int emit_fallback(JitBuf *buf, JitFn *fn_out) {
    if (jit_emit_prologue(buf, 0) != 0 ||
        jit_emit_call_r64(buf, (uint64_t)(uintptr_t)clvm_step) != 0 ||
        jit_emit_epilogue(buf) != 0)
        return -1;
    jit_seal(buf);
    *fn_out = (JitFn)(uintptr_t)buf->x;
    return 0;
}

static int image_can_jit(const ClvmImage *image) {
    uint32_t pc = 0;

    if (image == 0 || image->code == 0 || image->code_size == 0 ||
        image->code_size > JIT_MAX_PCS)
        return 0;
    while (pc < image->code_size) {
        int n = insn_len(image->code, pc, image->code_size);
        if (n <= 0)
            return 0;
        pc += (uint32_t)n;
    }
    return 1;
}

static int emit_cmp_eax_imm8(JitBuf *j, int8_t v) {
    return jit_emit_cmp_r32_imm8(j, 0, v);
}

int jit_compile_image(const ClvmImage *image, JitBuf *buf, JitFn *fn_out) {
    uint32_t fault, slice, halt, yield, dispatch, after_h, skip;
    uint32_t pc;
    uint32_t i;
    uint32_t table;
    uint8_t push_rbx[] = { 0x53 };
    uint8_t mov_bud[] = { 0x89, 0x75, 0xF0 };
    uint8_t ldpc[] = { 0x8B, 0x83 };
    uint8_t cmpa[] = { 0x3D };
    uint8_t movabs[] = { 0x48, 0xB9 };
    uint8_t ldx[] = { 0x8B, 0x04, 0x81 };
    uint8_t test[] = { 0x85, 0xC0 };
    uint8_t movabs2[] = { 0x48, 0xBA };
    uint8_t add[] = { 0x48, 0x01, 0xD0 };
    uint8_t jmprax[] = { 0xFF, 0xE0 };
    uint8_t ldst[] = { 0x8B, 0x83 };
    uint32_t tab_imm;
    uint32_t blob_imm;

    if (image == NULL || buf == NULL || fn_out == NULL)
        return -1;
    if (buf->w == NULL || buf->x == NULL) {
        uint64_t need;
        uint32_t pages;
        /* Native expansion is tens of bytes per opcode, not 24 MiB for UI. */
        need = (uint64_t)image->code_size * 64ull + 262144ull;
        pages = (uint32_t)((need + 4095ull) / 4096ull);
        if (pages < 64u) {
            pages = 64u;
        }
        if (pages > JIT_PAGES) {
            pages = JIT_PAGES;
        }
        buf->pages = pages;
        if (jit_alloc(buf) != 0) {
            serial_puts("jit: alloc failed\n");
            return -1;
        }
    }
    buf->used = 0;
    g_npatch = 0;
    serial_puts("jit: nat clear...\n");
    {
        uint32_t nclear = image->code_size;
        if (nclear > JIT_MAX_PCS) {
            nclear = JIT_MAX_PCS;
        }
        for (i = 0; i < nclear; ++i)
            g_nat[i] = 0;
    }
    serial_puts("jit: nat clear done\n");

    if (!image_can_jit(image)) {
        serial_puts("jit: fallback interpreter\n");
        if (emit_fallback(buf, fn_out) != 0) {
            jit_free(buf);
            return -1;
        }
        return 0;
    }
    serial_puts("jit: native compiling code_size=");
    serial_write_u64((uint64_t)image->code_size);
    serial_puts("\n");

    if (jit_emit_prologue(buf, 3) != 0 || jit_emit(buf, push_rbx, 1) != 0 ||
        jit_emit_mov_r64_r64(buf, REG_VM, REG_ARG0) != 0 ||
        jit_emit(buf, mov_bud, 3) != 0)
        goto fail;
    if (jit_emit_jmp_rel32(buf, 0) != 0)
        goto fail;
    {
        uint32_t enter_site = buf->used - 4u;
        uint32_t to_disp;

        fault = buf->used;
        if (emit_set_fault(buf, CLVM_FAULT_STACK_UNDERFLOW) != 0 ||
            emit_set_state(buf, CLVM_FAULTED) != 0 ||
            emit_ret_value(buf, CLVM_STEP_FAULT) != 0)
            goto fail;
        g_call_ovf = buf->used;
        if (emit_set_fault(buf, CLVM_FAULT_CALL_OVERFLOW) != 0 ||
            emit_set_state(buf, CLVM_FAULTED) != 0 ||
            emit_ret_value(buf, CLVM_STEP_FAULT) != 0)
            goto fail;
        g_fault_ba = buf->used;
        if (emit_set_fault_pc_eax(buf) != 0 ||
            emit_set_fault(buf, CLVM_FAULT_BAD_ADDRESS) != 0 ||
            emit_set_state(buf, CLVM_FAULTED) != 0 ||
            emit_ret_value(buf, CLVM_STEP_FAULT) != 0)
            goto fail;
        slice = buf->used;
        if (emit_ret_value(buf, CLVM_STEP_SLICE) != 0)
            goto fail;
        halt = buf->used;
        if (emit_ret_value(buf, CLVM_STEP_HALT) != 0)
            goto fail;
        yield = buf->used;
        if (emit_ret_value(buf, CLVM_STEP_YIELD) != 0)
            goto fail;
        /* Helper already stored the real fault. Do not rewrite it as
         * a stack underflow. */
        {
            uint32_t helper_fault = buf->used;
            if (emit_set_state(buf, CLVM_FAULTED) != 0 ||
                emit_ret_value(buf, CLVM_STEP_FAULT) != 0)
                goto fail;
            after_h = buf->used;
            if (jit_emit_test_r32_r32(buf, 0, 0) != 0 ||
                emit_js_to(buf, helper_fault) != 0 ||
            jit_emit(buf, ldst, 2) != 0 || jit_emit_u32(buf, JIT_OFF_STATE) != 0 ||
            emit_cmp_eax_imm8(buf, (int8_t)CLVM_WAITING) != 0 ||
            emit_je_to(buf, yield) != 0 ||
            jit_emit(buf, ldst, 2) != 0 || jit_emit_u32(buf, JIT_OFF_STATE) != 0 ||
            emit_cmp_eax_imm8(buf, (int8_t)CLVM_HALTED) != 0 ||
            emit_je_to(buf, halt) != 0 ||
            jit_emit(buf, ldst, 2) != 0 || jit_emit_u32(buf, JIT_OFF_STATE) != 0 ||
            emit_cmp_eax_imm8(buf, (int8_t)CLVM_FAULTED) != 0 ||
            emit_je_to(buf, helper_fault) != 0)
                goto fail;
        }
        if (jit_emit_jmp_rel32(buf, 0) != 0)
            goto fail;
        to_disp = buf->used - 4u;

        dispatch = buf->used;
        if (jit_emit(buf, ldpc, 2) != 0 || jit_emit_u32(buf, JIT_OFF_PC) != 0 ||
            jit_emit(buf, cmpa, 1) != 0 ||
            jit_emit_u32(buf, image->code_size) != 0 ||
            emit_jge_to(buf, fault) != 0)
            goto fail;
        tab_imm = buf->used + 2u;
        if (jit_emit(buf, movabs, 2) != 0 || jit_emit_u64(buf, 0) != 0)
            goto fail;
        if (jit_emit(buf, ldx, 3) != 0 || jit_emit(buf, test, 2) != 0 ||
            emit_je_to(buf, fault) != 0)
            goto fail;
        blob_imm = buf->used + 2u;
        if (jit_emit(buf, movabs2, 2) != 0 || jit_emit_u64(buf, 0) != 0)
            goto fail;
        if (jit_emit(buf, add, 3) != 0 || jit_emit(buf, jmprax, 2) != 0)
            goto fail;
        jit_patch_rel32(buf, to_disp, dispatch);

        skip = buf->used;
        if (jit_emit(buf, ldst, 2) != 0 || jit_emit_u32(buf, JIT_OFF_STATE) != 0 ||
            emit_cmp_eax_imm8(buf, (int8_t)CLVM_WAITING) != 0 ||
            emit_je_to(buf, yield) != 0 ||
            jit_emit(buf, ldst, 2) != 0 || jit_emit_u32(buf, JIT_OFF_STATE) != 0 ||
            emit_cmp_eax_imm8(buf, (int8_t)CLVM_HALTED) != 0 ||
            emit_je_to(buf, halt) != 0)
            goto fail;
        if (emit_jmp_to(buf, dispatch) != 0)
            goto fail;
        jit_patch_rel32(buf, enter_site, skip);
    }

    pc = 0;
    while (pc < image->code_size) {
        int n = insn_len(image->code, pc, image->code_size);
        g_nat[pc] = buf->used;
        if (emit_insn(buf, image->code, pc, image->code_size, fault, slice,
                      halt, dispatch, after_h) != 0) {
            serial_puts("jit: emit_insn fail pc=");
            serial_write_u64((uint64_t)pc);
            serial_puts(" used=");
            serial_write_u64((uint64_t)buf->used);
            serial_puts(" cap=");
            serial_write_u64((uint64_t)buf->cap);
            serial_puts("\n");
            goto fail;
        }
        pc += (uint32_t)n;
        if ((pc & 0xffffu) == 0u) {
            serial_puts("jit: emit pc=");
            serial_write_u64((uint64_t)pc);
            serial_puts(" used=");
            serial_write_u64((uint64_t)buf->used);
            serial_puts("\n");
            gc_poll();
        }
    }

    table = buf->used;
    for (i = 0; i < image->code_size; ++i) {
        if (jit_emit_u32(buf, g_nat[i]) != 0)
            goto fail;
    }

    {
        uint64_t tab = (uint64_t)(uintptr_t)buf->x + table;
        uint64_t blob = (uint64_t)(uintptr_t)buf->x;
        uint32_t k;
        for (k = 0; k < 8; ++k) {
            buf->w[tab_imm + k] = (uint8_t)(tab >> (8 * k));
            buf->w[blob_imm + k] = (uint8_t)(blob >> (8 * k));
        }
    }

    for (i = 0; i < g_npatch; ++i) {
        uint32_t tgt = g_ptgt[i];
        if (tgt >= image->code_size || g_nat[tgt] == 0)
            goto fail;
        jit_patch_rel32(buf, g_psite[i], g_nat[tgt]);
    }

    jit_seal(buf);
    *fn_out = (JitFn)(uintptr_t)buf->x;
    serial_puts("jit: native ready\n");
    return 0;

fail:
    serial_puts("jit: native emit failed\n");
    jit_free(buf);
    return -1;
}
