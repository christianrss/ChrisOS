#include "jit_compile.h"

#include "jit_emit.h"
#include "jit_runtime.h"

#define JIT_OFF_PC     12u
#define JIT_OFF_STATE  66856u
#define REG_VM   3   /* rbx */
#define REG_ARG0 7   /* rdi */
#define REG_ARG1 6   /* rsi */

extern ClvmStepResult clvm_step(ClvmVm *vm, uint32_t budget);

static int insn_len(const uint8_t *code, uint32_t pc, uint32_t size) {
    uint8_t op;

    if (pc >= size)
        return 0;
    op = code[pc];
    switch (op) {
    case CL_OP_NOP:
    case CL_OP_ADD:
    case CL_OP_SUB:
    case CL_OP_MUL:
    case CL_OP_DIV:
    case CL_OP_MOD:
    case CL_OP_DUP:
    case CL_OP_PRINT:
    case CL_OP_HALT:
    case CL_OP_RET:
    case CL_OP_DROP:
    case CL_OP_SWAP:
    case CL_OP_EQ:
    case CL_OP_LT:
    case CL_OP_LE:
    case CL_OP_GT:
    case CL_OP_GE:
    case CL_OP_NEG:
    case CL_OP_NE:
    case CL_OP_SYS:
    case CL_OP_LOAD:
    case CL_OP_STORE:
    case CL_OP_FLOAD:
    case CL_OP_FSTORE:
    case CL_OP_FADD:
    case CL_OP_FSUB:
    case CL_OP_FMUL:
    case CL_OP_FDIV:
    case CL_OP_FNEG:
    case CL_OP_FTOI:
    case CL_OP_ITOF:
    case CL_OP_FEQ:
    case CL_OP_FLT:
    case CL_OP_FLE:
        return 1;
    case CL_OP_PUSH:
    case CL_OP_FPUSH:
        return 5;
    case CL_OP_JMP:
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_CALL:
        return 3;
    default:
        return -1;
    }
}

static int jit_supported(uint8_t op) {
    switch (op) {
    case CL_OP_NOP:
    case CL_OP_PUSH:
    case CL_OP_ADD:
    case CL_OP_SUB:
    case CL_OP_MUL:
    case CL_OP_DIV:
    case CL_OP_MOD:
    case CL_OP_LOAD:
    case CL_OP_STORE:
    case CL_OP_JMP:
    case CL_OP_JZ:
    case CL_OP_JNZ:
    case CL_OP_SYS:
    case CL_OP_HALT:
    case CL_OP_DROP:
    case CL_OP_NEG:
    case CL_OP_EQ:
    case CL_OP_NE:
    case CL_OP_LT:
    case CL_OP_LE:
    case CL_OP_GT:
    case CL_OP_GE:
    case CL_OP_DUP:
    case CL_OP_SWAP:
    case CL_OP_CALL:
    case CL_OP_RET:
    case CL_OP_PRINT:
    case CL_OP_FLOAD:
    case CL_OP_FSTORE:
    case CL_OP_FPUSH:
    case CL_OP_FADD:
    case CL_OP_FSUB:
    case CL_OP_FMUL:
    case CL_OP_FDIV:
    case CL_OP_FNEG:
    case CL_OP_FTOI:
    case CL_OP_ITOF:
    case CL_OP_FEQ:
    case CL_OP_FLT:
    case CL_OP_FLE:
        return 1;
    default:
        return 0;
    }
}

static int image_supported(const ClvmImage *image) {
    uint32_t pc = 0;

    if (image == 0 || image->code == 0)
        return 0;
    while (pc < image->code_size) {
        int n = insn_len(image->code, pc, image->code_size);
        if (n <= 0 || !jit_supported(image->code[pc]))
            return 0;
        pc += (uint32_t)n;
    }
    return 1;
}

static int find_loop(const ClvmImage *image, uint32_t *loop_start, uint32_t *loop_end) {
    uint32_t pc = 0;
    int found = 0;
    uint32_t best_start = 0;
    uint32_t best_end = 0;

    if (image == 0 || loop_start == 0 || loop_end == 0)
        return 0;
    while (pc < image->code_size) {
        uint8_t op = image->code[pc];
        int n = insn_len(image->code, pc, image->code_size);
        if (n <= 0)
            return 0;
        if (op == CL_OP_JMP || op == CL_OP_JZ || op == CL_OP_JNZ) {
            int16_t rel = (int16_t)((uint16_t)image->code[pc + 1u] |
                                    ((uint16_t)image->code[pc + 2u] << 8));
            uint32_t target = pc + 3u + (uint32_t)(int32_t)rel;
            if (target < pc && (!found || target > best_start)) {
                best_start = target;
                best_end = pc;
                found = 1;
            }
        }
        pc += (uint32_t)n;
    }
    if (found) {
        *loop_start = best_start;
        *loop_end = best_end;
    }
    return found;
}

static int emit_vm_prologue(JitBuf *j) {
    static const uint8_t push_rbx[] = { 0x53 };
    static const uint8_t mov_bud[] = { 0x89, 0x75, 0xf0 }; /* mov [rbp-16], esi */

    if (jit_emit_prologue(j, 0) != 0 ||
        jit_emit(j, push_rbx, 1) != 0 ||
        jit_emit_mov_r64_r64(j, REG_VM, REG_ARG0) != 0 ||
        jit_emit(j, mov_bud, 3) != 0)
        return -1;
    return 0;
}

static int emit_vm_epilogue(JitBuf *j) {
    static const uint8_t pop_rbx[] = { 0x5B };

    if (jit_emit(j, pop_rbx, 1) != 0 ||
        jit_emit_epilogue(j) != 0)
        return -1;
    return 0;
}

static int emit_exec_at_pc(JitBuf *j) {
    return jit_emit_mov_r64_r64(j, REG_ARG0, REG_VM) != 0 ||
           jit_emit_call_r64(j, (uint64_t)(uintptr_t)jit_rt_exec_at_pc) != 0;
}

static int emit_ret_value(JitBuf *j, ClvmStepResult value) {
    return jit_emit_mov_r32_imm(j, 0, (int32_t)value) != 0 ||
           emit_vm_epilogue(j) != 0;
}

static int emit_dec_budget(JitBuf *j) {
    static const uint8_t dec[] = { 0xff, 0x4d, 0xf0 }; /* dec dword [rbp-16] */
    return jit_emit(j, dec, 3);
}

static int emit_budget_zero(JitBuf *j) {
    static const uint8_t cmp0[] = { 0x83, 0x7d, 0xf0, 0x00 }; /* cmp dword [rbp-16], 0 */
    return jit_emit(j, cmp0, 4);
}

static int emit_load_budget_arg(JitBuf *j) {
    static const uint8_t mov[] = { 0x8b, 0x75, 0xf0 }; /* mov esi, [rbp-16] */
    return jit_emit(j, mov, 3);
}

static int emit_run_range(JitBuf *j, uint32_t start, uint32_t end) {
    uint32_t fault_jmp;

    if (jit_emit_mov_r64_r64(j, REG_ARG0, REG_VM) != 0 ||
        jit_emit_mov_r32_imm(j, REG_ARG1, (int32_t)start) != 0 ||
        jit_emit_mov_r32_imm(j, 2, (int32_t)end) != 0 ||
        jit_emit_call_r64(j, (uint64_t)(uintptr_t)jit_rt_run_range) != 0)
        return -1;
    if (jit_emit_cmp_r32_imm8(j, 0, 0) != 0 ||
        jit_emit_jge_rel32(j, 0) != 0)
        return -1;
    fault_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_FAULT) != 0)
        return -1;
    jit_patch_rel32(j, fault_jmp, j->used);
    return 0;
}

static int emit_state_yield_halt(JitBuf *j, uint32_t *yield_jmp, uint32_t *halt_jmp) {
    if (jit_emit_mov_r32_from_mem_disp(j, 0, REG_VM, JIT_OFF_STATE) != 0 ||
        jit_emit_cmp_r32_imm8(j, 0, (int8_t)CLVM_WAITING) != 0 ||
        jit_emit_jne_rel32(j, 0) != 0)
        return -1;
    *yield_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_YIELD) != 0)
        return -1;
    jit_patch_rel32(j, *yield_jmp, j->used);

    if (jit_emit_mov_r32_from_mem_disp(j, 0, REG_VM, JIT_OFF_STATE) != 0 ||
        jit_emit_cmp_r32_imm8(j, 0, (int8_t)CLVM_HALTED) != 0 ||
        jit_emit_jne_rel32(j, 0) != 0)
        return -1;
    *halt_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_HALT) != 0)
        return -1;
    jit_patch_rel32(j, *halt_jmp, j->used);
    return 0;
}

static int emit_post_step_checks(JitBuf *j, uint32_t *yield_jmp, uint32_t *fault_jmp) {
    if (jit_emit_mov_r32_from_mem_disp(j, 0, REG_VM, JIT_OFF_STATE) != 0 ||
        jit_emit_cmp_r32_imm8(j, 0, (int8_t)CLVM_WAITING) != 0 ||
        jit_emit_jne_rel32(j, 0) != 0)
        return -1;
    *yield_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_YIELD) != 0)
        return -1;
    jit_patch_rel32(j, *yield_jmp, j->used);

    if (jit_emit_mov_r32_from_mem_disp(j, 0, REG_VM, JIT_OFF_STATE) != 0 ||
        jit_emit_cmp_r32_imm8(j, 0, (int8_t)CLVM_FAULTED) != 0 ||
        jit_emit_jne_rel32(j, 0) != 0)
        return -1;
    *fault_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_FAULT) != 0)
        return -1;
    jit_patch_rel32(j, *fault_jmp, j->used);
    return 0;
}

static int emit_exec_fault_check(JitBuf *j) {
    uint32_t fault_jmp;

    if (emit_exec_at_pc(j) != 0)
        return -1;
    if (jit_emit_cmp_r32_imm8(j, 0, 0) != 0 ||
        jit_emit_jge_rel32(j, 0) != 0)
        return -1;
    fault_jmp = j->used - 4u;
    if (emit_ret_value(j, CLVM_STEP_FAULT) != 0)
        return -1;
    jit_patch_rel32(j, fault_jmp, j->used);
    return 0;
}

static int emit_native_dispatch(JitBuf *buf, JitFn *fn_out) {
    uint32_t loop_top;
    uint32_t yield_jmp;
    uint32_t halt_jmp;
    uint32_t slice_jmp;
    uint32_t yield2_jmp;
    uint32_t fault2_jmp;

    if (emit_vm_prologue(buf) != 0)
        return -1;
    if (emit_state_yield_halt(buf, &yield_jmp, &halt_jmp) != 0)
        return -1;

    loop_top = buf->used;
    if (emit_budget_zero(buf) != 0 ||
        jit_emit_jne_rel32(buf, 0) != 0)
        return -1;
    slice_jmp = buf->used - 4u;
    if (emit_ret_value(buf, CLVM_STEP_SLICE) != 0)
        return -1;
    jit_patch_rel32(buf, slice_jmp, buf->used);

    if (emit_exec_fault_check(buf) != 0 ||
        emit_dec_budget(buf) != 0 ||
        emit_post_step_checks(buf, &yield2_jmp, &fault2_jmp) != 0 ||
        jit_emit_jmp_rel32(buf, 0) != 0)
        return -1;
    jit_patch_rel32(buf, buf->used - 4u, loop_top);

    jit_seal(buf);
    *fn_out = (JitFn)(uintptr_t)buf->x;
    return 0;
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

int jit_compile_image(const ClvmImage *image, JitBuf *buf, JitFn *fn_out) {
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    uint32_t loop_top;
    uint32_t skip_init_jmp;
    uint32_t yield_jmp;
    uint32_t halt_jmp;
    uint32_t slice_jmp;
    uint32_t yield2_jmp;
    uint32_t fault2_jmp;

    if (image == NULL || buf == NULL || fn_out == NULL)
        return -1;
    if (jit_alloc(buf) != 0)
        return -1;

    if (!image_supported(image)) {
        if (emit_fallback(buf, fn_out) != 0) {
            jit_free(buf);
            return -1;
        }
        return 0;
    }

    if (!find_loop(image, &loop_start, &loop_end)) {
        if (emit_native_dispatch(buf, fn_out) != 0) {
            jit_free(buf);
            return -1;
        }
        return 0;
    }

    if (emit_vm_prologue(buf) != 0)
        goto fail;

    if (emit_state_yield_halt(buf, &yield_jmp, &halt_jmp) != 0)
        goto fail;

    if (jit_emit_mov_r32_from_mem_disp(buf, 0, REG_VM, JIT_OFF_PC) != 0 ||
        jit_emit_cmp_r32_imm32(buf, 0, (int32_t)loop_start) != 0 ||
        jit_emit_jge_rel32(buf, 0) != 0)
        goto fail;
    skip_init_jmp = buf->used - 4u;
    if (jit_emit_mov_r64_r64(buf, REG_ARG0, REG_VM) != 0 ||
        emit_load_budget_arg(buf) != 0 ||
        jit_emit_call_r64(buf, (uint64_t)(uintptr_t)clvm_step) != 0 ||
        emit_vm_epilogue(buf) != 0)
        goto fail;
    jit_patch_rel32(buf, skip_init_jmp, buf->used);

    loop_top = buf->used;
    if (emit_budget_zero(buf) != 0 ||
        jit_emit_jne_rel32(buf, 0) != 0)
        goto fail;
    slice_jmp = buf->used - 4u;
    if (emit_ret_value(buf, CLVM_STEP_SLICE) != 0)
        goto fail;
    jit_patch_rel32(buf, slice_jmp, buf->used);

    if (emit_run_range(buf, loop_start, loop_end) != 0)
        goto fail;

    if (emit_dec_budget(buf) != 0 ||
        emit_post_step_checks(buf, &yield2_jmp, &fault2_jmp) != 0)
        goto fail;

    if (jit_emit_jmp_rel32(buf, 0) != 0)
        goto fail;
    jit_patch_rel32(buf, buf->used - 4u, loop_top);

    jit_seal(buf);
    *fn_out = (JitFn)(uintptr_t)buf->x;
    return 0;

fail:
    jit_free(buf);
    return -1;
}
