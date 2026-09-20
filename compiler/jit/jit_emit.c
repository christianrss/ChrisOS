#include "jit_emit.h"

static int rex_w(int reg) {
    return (reg & 8) ? 0x49 : 0x48;
}

static int reg_lo(int reg) {
    return reg & 7;
}

int jit_emit_u8(JitBuf *j, uint8_t b) {
    return jit_emit(j, &b, 1);
}

int jit_emit_u32(JitBuf *j, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    return jit_emit(j, b, 4);
}

int jit_emit_u64(JitBuf *j, uint64_t v) {
    uint8_t b[8];
    int i;
    for (i = 0; i < 8; ++i) {
        b[i] = (uint8_t)(v >> (8 * i));
    }
    return jit_emit(j, b, 8);
}

int jit_emit_ret(JitBuf *j) {
    uint8_t b = 0xC3;
    return jit_emit(j, &b, 1);
}

int jit_emit_mov_r32_imm(JitBuf *j, int reg, int32_t v) {
    uint8_t b[6];
    uint32_t n = 0;

    if (reg >= 8) {
        b[n++] = (uint8_t)rex_w(reg);
    }
    b[n++] = (uint8_t)(0xB8 + reg_lo(reg));
    b[n++] = (uint8_t)(uint32_t)v;
    b[n++] = (uint8_t)((uint32_t)v >> 8);
    b[n++] = (uint8_t)((uint32_t)v >> 16);
    b[n++] = (uint8_t)((uint32_t)v >> 24);
    return jit_emit(j, b, n);
}

int jit_emit_mov_r64_imm(JitBuf *j, int reg, uint64_t v) {
    uint8_t b[10];
    b[0] = (uint8_t)rex_w(reg);
    b[1] = (uint8_t)(0xB8 + reg_lo(reg));
    b[2] = (uint8_t)v;
    b[3] = (uint8_t)(v >> 8);
    b[4] = (uint8_t)(v >> 16);
    b[5] = (uint8_t)(v >> 24);
    b[6] = (uint8_t)(v >> 32);
    b[7] = (uint8_t)(v >> 40);
    b[8] = (uint8_t)(v >> 48);
    b[9] = (uint8_t)(v >> 56);
    return jit_emit(j, b, 10);
}

int jit_emit_mov_r64_r64(JitBuf *j, int dst, int src) {
    uint8_t b[3];
    b[0] = (uint8_t)(0x48 | ((dst & 8) ? 4 : 0) | ((src & 8) ? 1 : 0));
    b[1] = 0x89;
    b[2] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
    return jit_emit(j, b, 3);
}

int jit_emit_add_r32_r32(JitBuf *j, int dst, int src) {
    uint8_t b[3];
    if (dst >= 8 || src >= 8) {
        b[0] = (uint8_t)(0x40 | ((dst & 8) ? 4 : 0) | ((src & 8) ? 1 : 0));
        b[1] = 0x01;
        b[2] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
        return jit_emit(j, b, 3);
    }
    b[0] = 0x01;
    b[1] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
    return jit_emit(j, b, 2);
}

int jit_emit_sub_r32_r32(JitBuf *j, int dst, int src) {
    uint8_t b[3];
    if (dst >= 8 || src >= 8) {
        b[0] = (uint8_t)(0x40 | ((dst & 8) ? 4 : 0) | ((src & 8) ? 1 : 0));
        b[1] = 0x29;
        b[2] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
        return jit_emit(j, b, 3);
    }
    b[0] = 0x29;
    b[1] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
    return jit_emit(j, b, 2);
}

int jit_emit_imul_r32_r32(JitBuf *j, int dst, int src) {
    uint8_t b[4];
    if (dst >= 8 || src >= 8) {
        b[0] = (uint8_t)(0x40 | ((dst & 8) ? 4 : 0) | ((src & 8) ? 1 : 0));
        b[1] = 0x0F;
        b[2] = 0xAF;
        b[3] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
        return jit_emit(j, b, 4);
    }
    b[0] = 0x0F;
    b[1] = 0xAF;
    b[2] = (uint8_t)(0xC0 | ((src & 7) << 3) | (dst & 7));
    return jit_emit(j, b, 3);
}

int jit_emit_cmp_r32_r32(JitBuf *j, int a, int breg) {
    uint8_t b[3];
    if (a >= 8 || breg >= 8) {
        b[0] = (uint8_t)(0x40 | ((a & 8) ? 4 : 0) | ((breg & 8) ? 1 : 0));
        b[1] = 0x39;
        b[2] = (uint8_t)(0xC0 | ((breg & 7) << 3) | (a & 7));
        return jit_emit(j, b, 3);
    }
    b[0] = 0x39;
    b[1] = (uint8_t)(0xC0 | ((breg & 7) << 3) | (a & 7));
    return jit_emit(j, b, 2);
}

int jit_emit_cmp_r32_imm32(JitBuf *j, int reg, int32_t imm) {
    uint8_t b[7];
    int n = 0;

    if (reg >= 8) {
        b[n++] = (uint8_t)(0x40 | ((reg & 8) ? 4 : 0));
    }
    b[n++] = 0x81;
    b[n++] = (uint8_t)(0xF8 | reg_lo(reg));
    b[n++] = (uint8_t)imm;
    b[n++] = (uint8_t)(imm >> 8);
    b[n++] = (uint8_t)(imm >> 16);
    b[n++] = (uint8_t)(imm >> 24);
    return jit_emit(j, b, (uint32_t)n);
}

int jit_emit_cmp_r32_imm8(JitBuf *j, int reg, int8_t imm) {
    uint8_t b[4];
    if (reg >= 8) {
        b[0] = (uint8_t)(0x40 | ((reg & 8) ? 4 : 0));
        b[1] = 0x83;
        b[2] = (uint8_t)(0xF8 | reg_lo(reg));
        b[3] = (uint8_t)imm;
        return jit_emit(j, b, 4);
    }
    b[0] = 0x83;
    b[1] = (uint8_t)(0xF8 | reg_lo(reg));
    b[2] = (uint8_t)imm;
    return jit_emit(j, b, 3);
}

int jit_emit_test_r32_r32(JitBuf *j, int a, int breg) {
    uint8_t b[3];
    if (a >= 8 || breg >= 8) {
        b[0] = (uint8_t)(0x40 | ((a & 8) ? 4 : 0) | ((breg & 8) ? 1 : 0));
        b[1] = 0x85;
        b[2] = (uint8_t)(0xC0 | ((breg & 7) << 3) | (a & 7));
        return jit_emit(j, b, 3);
    }
    b[0] = 0x85;
    b[1] = (uint8_t)(0xC0 | ((breg & 7) << 3) | (a & 7));
    return jit_emit(j, b, 2);
}

int jit_emit_je_rel32(JitBuf *j, int32_t rel) {
    uint8_t b[6];
    b[0] = 0x0F;
    b[1] = 0x84;
    b[2] = (uint8_t)rel;
    b[3] = (uint8_t)(rel >> 8);
    b[4] = (uint8_t)(rel >> 16);
    b[5] = (uint8_t)(rel >> 24);
    return jit_emit(j, b, 6);
}

int jit_emit_jne_rel32(JitBuf *j, int32_t rel) {
    uint8_t b[6];
    b[0] = 0x0F;
    b[1] = 0x85;
    b[2] = (uint8_t)rel;
    b[3] = (uint8_t)(rel >> 8);
    b[4] = (uint8_t)(rel >> 16);
    b[5] = (uint8_t)(rel >> 24);
    return jit_emit(j, b, 6);
}

int jit_emit_jge_rel32(JitBuf *j, int32_t rel) {
    uint8_t b[6];
    b[0] = 0x0F;
    b[1] = 0x8D;
    b[2] = (uint8_t)rel;
    b[3] = (uint8_t)(rel >> 8);
    b[4] = (uint8_t)(rel >> 16);
    b[5] = (uint8_t)(rel >> 24);
    return jit_emit(j, b, 6);
}

int jit_emit_jmp_rel32(JitBuf *j, int32_t rel) {
    uint8_t b[5];
    b[0] = 0xE9;
    b[1] = (uint8_t)rel;
    b[2] = (uint8_t)(rel >> 8);
    b[3] = (uint8_t)(rel >> 16);
    b[4] = (uint8_t)(rel >> 24);
    return jit_emit(j, b, 5);
}

int jit_emit_call_r64(JitBuf *j, uint64_t target) {
    static const uint8_t call_rax[] = { 0xFF, 0xD0 };

    if (jit_emit_mov_r64_imm(j, 0, target) != 0) {
        return -1;
    }
    return jit_emit(j, call_rax, 2);
}

int jit_emit_prologue(JitBuf *j, int locals) {
    static const uint8_t push_rbp[] = { 0x55 };
    static const uint8_t mov_rbp_rsp[] = { 0x48, 0x89, 0xE5 };
    uint8_t sub_rsp[4];

    if (jit_emit(j, push_rbp, 1) != 0 ||
        jit_emit(j, mov_rbp_rsp, 3) != 0) {
        return -1;
    }
    if (locals <= 0) {
        return 0;
    }
    sub_rsp[0] = 0x48;
    sub_rsp[1] = 0x83;
    sub_rsp[2] = 0xEC;
    sub_rsp[3] = (uint8_t)(locals * 8);
    return jit_emit(j, sub_rsp, 4);
}

int jit_emit_epilogue(JitBuf *j) {
    static const uint8_t leave_ret[] = { 0xC9, 0xC3 };
    return jit_emit(j, leave_ret, 2);
}

int jit_emit_mov_r32_from_mem_disp(JitBuf *j, int dst, int base, uint32_t disp) {
    uint8_t b[7];
    int n = 0;

    if (dst >= 8 || base >= 8) {
        b[n++] = (uint8_t)(0x40 | ((dst & 8) ? 4 : 0) | ((base & 8) ? 1 : 0));
    }
    b[n++] = 0x8B;
    b[n++] = (uint8_t)(0x80 | ((dst & 7) << 3) | (base & 7));
    b[n++] = (uint8_t)disp;
    b[n++] = (uint8_t)(disp >> 8);
    b[n++] = (uint8_t)(disp >> 16);
    b[n++] = (uint8_t)(disp >> 24);
    return jit_emit(j, b, (uint32_t)n);
}

int jit_emit_mov_mem_disp_r32(JitBuf *j, uint32_t disp, int base, int src) {
    uint8_t b[7];
    int n = 0;

    if (src >= 8 || base >= 8) {
        b[n++] = (uint8_t)(0x40 | ((src & 8) ? 4 : 0) | ((base & 8) ? 1 : 0));
    }
    b[n++] = 0x89;
    b[n++] = (uint8_t)(0x80 | ((src & 7) << 3) | (base & 7));
    b[n++] = (uint8_t)disp;
    b[n++] = (uint8_t)(disp >> 8);
    b[n++] = (uint8_t)(disp >> 16);
    b[n++] = (uint8_t)(disp >> 24);
    return jit_emit(j, b, (uint32_t)n);
}

int jit_emit_cmp_mem_disp_imm8(JitBuf *j, uint32_t disp, int base, int8_t imm) {
    uint8_t b[8];
    int n = 0;

    if (base >= 8) {
        b[n++] = (uint8_t)(0x40 | ((base & 8) ? 1 : 0));
    }
    b[n++] = 0x83;
    b[n++] = (uint8_t)(0xB8 | (base & 7));
    b[n++] = (uint8_t)disp;
    b[n++] = (uint8_t)(disp >> 8);
    b[n++] = (uint8_t)(disp >> 16);
    b[n++] = (uint8_t)(disp >> 24);
    b[n++] = (uint8_t)imm;
    return jit_emit(j, b, (uint32_t)n);
}

int jit_emit_test_r32_imm(JitBuf *j, int reg, int32_t imm) {
    if (imm == 0) {
        return jit_emit_test_r32_r32(j, reg, reg);
    }
    return jit_emit_cmp_r32_imm8(j, reg, (int8_t)imm);
}

int jit_emit_je_rel8(JitBuf *j, int8_t rel) {
    uint8_t b[2] = { 0x74, (uint8_t)rel };
    return jit_emit(j, b, 2);
}

int jit_emit_jmp_rel8(JitBuf *j, int8_t rel) {
    uint8_t b[2] = { 0xEB, (uint8_t)rel };
    return jit_emit(j, b, 2);
}

void jit_patch_rel32(JitBuf *j, uint32_t site, uint32_t target) {
    int32_t rel;

    if (j == 0 || j->w == 0 || site + 4u > j->used) {
        return;
    }
    rel = (int32_t)(target - (site + 4u));
    j->w[site] = (uint8_t)rel;
    j->w[site + 1u] = (uint8_t)(rel >> 8);
    j->w[site + 2u] = (uint8_t)(rel >> 16);
    j->w[site + 3u] = (uint8_t)(rel >> 24);
}
