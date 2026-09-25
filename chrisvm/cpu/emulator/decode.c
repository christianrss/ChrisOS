#include "chris_arch.h"

#include <stdio.h>
#include <string.h>

static uint32_t ru32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t ru16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static int is_prefix(uint8_t p) {
    return p == 0x66 || p == 0x67 || p == 0xf0 || p == 0xf2 || p == 0xf3 || p == 0x26 || p == 0x2e ||
           p == 0x36 || p == 0x3e || p == 0x64 || p == 0x65 || (p >= 0x40 && p <= 0x4f);
}

static int read_modrm(const uint8_t *b, int avail, int i, ChrisInsn *in) {
    uint8_t m;
    int addr64 = in->asz == 8;
    if (i >= avail) {
        return -1;
    }
    m = b[i++];
    in->has_modrm = 1;
    in->mod = m >> 6;
    in->digit = (m >> 3) & 7;
    in->reg = in->digit | (in->rex_r ? 8 : 0);
    in->rm_field = m & 7;
    in->rm = in->rm_field | (in->rex_b ? 8 : 0);
    if (in->mod != 3 && in->rm_field == 4) {
        uint8_t s;
        if (i >= avail) {
            return -1;
        }
        s = b[i++];
        in->has_sib = 1;
        in->scale = s >> 6;
        in->base_field = s & 7;
        in->base = in->base_field | (in->rex_b ? 8 : 0);
        in->index_field = (s >> 3) & 7;
        in->index = in->index_field | (in->rex_x ? 8 : 0);
        in->no_index = in->index_field == 4 && !in->rex_x;
        if (in->mod == 0 && in->base_field == 5) {
            in->no_base = 1;
        }
    }
    if (in->mod == 0 && !in->has_sib && in->rm_field == 5) {
        if (addr64) {
            in->rip_rel = 1;
        }
        in->has_disp = 1;
        if (i + 4 > avail) {
            return -1;
        }
        in->disp = (int32_t)ru32(b + i);
        i += 4;
    } else if (in->mod == 1) {
        if (i >= avail) {
            return -1;
        }
        in->has_disp = 1;
        in->disp = (int8_t)b[i++];
    } else if (in->mod == 2 || (in->mod == 0 && in->no_base)) {
        if (i + 4 > avail) {
            return -1;
        }
        in->has_disp = 1;
        in->disp = (int32_t)ru32(b + i);
        i += 4;
    }
    return i;
}

static int imm_n(const uint8_t *b, int avail, int i, ChrisInsn *in, int n) {
    if (i + n > avail) {
        return -1;
    }
    if (n == 1) {
        in->imm = b[i];
    } else if (n == 2) {
        in->imm = ru16(b + i);
    } else if (n == 4) {
        in->imm = ru32(b + i);
    } else if (n == 8) {
        uint64_t lo = ru32(b + i);
        uint64_t hi = ru32(b + i + 4);
        in->imm = lo | (hi << 32);
    } else {
        return -1;
    }
    in->imm_bytes = n;
    return i + n;
}

static void alu_row(ChrisInsn *in, int op) {
    static const int map[8] = {CHRIS_ALU_ADD, CHRIS_ALU_OR,  CHRIS_ALU_ADC, CHRIS_ALU_SBB,
                               CHRIS_ALU_AND, CHRIS_ALU_SUB, CHRIS_ALU_XOR, CHRIS_ALU_CMP};
    int row = (op >> 3) & 7;
    int low = op & 7;
    in->op = CHRIS_OP_ALU;
    in->alu = map[row];
    if (low == 0) {
        in->os = 1;
        in->form = CHRIS_FORM_RM_REG;
    } else if (low == 1) {
        in->form = CHRIS_FORM_RM_REG;
    } else if (low == 2) {
        in->os = 1;
        in->form = CHRIS_FORM_REG_RM;
    } else if (low == 3) {
        in->form = CHRIS_FORM_REG_RM;
    } else if (low == 4) {
        in->os = 1;
        in->form = CHRIS_FORM_ACC_IMM;
        in->acc_imm = 1;
    } else if (low == 5) {
        in->form = CHRIS_FORM_ACC_IMM;
        in->acc_imm = 1;
    }
}

static const char *reg_name(int os, int rex, int reg) {
    static const char *r64[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    static const char *r32[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
                                "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
    static const char *r16[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
                                "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
    static const char *r8[] = {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
                               "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
    static const char *hi[] = {"ah", "ch", "dh", "bh"};
    if (reg < 0 || reg > 15) {
        return "?";
    }
    if (os == 8) {
        return r64[reg];
    }
    if (os == 4) {
        return r32[reg];
    }
    if (os == 2) {
        return r16[reg];
    }
    if (!rex && reg >= 4 && reg <= 7) {
        return hi[reg - 4];
    }
    return r8[reg];
}

static const char *alu_name(int alu) {
    static const char *n[] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP", "TEST"};
    if (alu < 0 || alu > 8) {
        return "ALU";
    }
    return n[alu];
}

static const char *cc_name(int cc) {
    static const char *n[] = {"o", "no", "b", "ae", "e", "ne", "be", "a",
                              "s", "ns", "p", "np", "l", "ge", "le", "g"};
    return n[cc & 15];
}

void chris_format_insn(const ChrisInsn *in, char *dst, int cap) {
    if (!dst || cap < 2) {
        return;
    }
    dst[0] = 0;
    if (!in) {
        return;
    }
    if (in->op == CHRIS_OP_ALU) {
        snprintf(dst, (size_t)cap, "%s", alu_name(in->alu));
    } else if (in->op == CHRIS_OP_JCC) {
        snprintf(dst, (size_t)cap, "J%s", cc_name(in->cc));
    } else if (in->op == CHRIS_OP_MOV && in->has_modrm && in->mod != 3) {
        const char *reg = reg_name(in->os, in->rex, in->reg);
        if (in->form == CHRIS_FORM_RM_IMM) {
            snprintf(dst, (size_t)cap, "MOV [mem], 0x%llx", (unsigned long long)in->imm);
        } else if (in->form == CHRIS_FORM_RM_REG) {
            snprintf(dst, (size_t)cap, "MOV [mem], %s", reg);
        } else {
            snprintf(dst, (size_t)cap, "MOV %s, [mem]", reg);
        }
    } else if (in->op == CHRIS_OP_MOV && in->has_modrm) {
        if (in->form == CHRIS_FORM_RM_REG) {
            snprintf(dst, (size_t)cap, "MOV %s, %s", reg_name(in->os, in->rex, in->rm),
                     reg_name(in->os, in->rex, in->reg));
        } else {
            snprintf(dst, (size_t)cap, "MOV %s, %s", reg_name(in->os, in->rex, in->reg),
                     reg_name(in->os, in->rex, in->rm));
        }
    } else if (in->op == CHRIS_OP_MOV && in->reg_only_push) {
        snprintf(dst, (size_t)cap, "MOV %s, 0x%llx", reg_name(in->os, in->rex, in->rm),
                 (unsigned long long)in->imm);
    } else {
        snprintf(dst, (size_t)cap, "%s", chris_op_name(in->op));
    }
}

int chris_decode(const uint8_t *bytes, int avail, ChrisInsn *out) {
    ChrisInsn in;
    int i = 0;
    int os16 = 0;
    int as32 = 0;
    int op;
    int two = 0;
    if (!bytes || !out || avail <= 0) {
        return -1;
    }
    memset(&in, 0, sizeof in);
    in.os = 4;
    in.asz = 8;
    while (i < avail && i < 14 && is_prefix(bytes[i])) {
        uint8_t p = bytes[i++];
        if (p == 0x66) {
            os16 = 1;
            in.rex = 0;
        } else if (p == 0x67) {
            as32 = 1;
            in.rex = 0;
        } else if (p == 0xf0) {
            in.lock = 1;
            in.rex = 0;
        } else if (p == 0xf2 || p == 0xf3) {
            in.rex = 0;
        } else if (p == 0x26 || p == 0x2e || p == 0x36 || p == 0x3e || p == 0x64 || p == 0x65) {
            in.rex = 0;
        } else {
            in.rex = p;
        }
    }
    if (i >= avail) {
        return -1;
    }
    if (in.rex) {
        in.rex_w = (in.rex & 8) != 0;
        in.rex_r = (in.rex & 4) != 0;
        in.rex_x = (in.rex & 2) != 0;
        in.rex_b = (in.rex & 1) != 0;
    }
    in.os = in.rex_w ? 8 : os16 ? 2 : 4;
    in.asz = as32 ? 4 : 8;
    op = bytes[i++];
    if (op == 0x0f) {
        if (i >= avail) {
            return -1;
        }
        op = bytes[i++];
        two = 1;
    }
    if (!two && op <= 0x3f && (op & 7) <= 5 && (op & 7) != 6) {
        int low = op & 7;
        if ((op & 7) >= 6) {
            in.op = CHRIS_OP_UD;
        } else {
            alu_row(&in, op);
            if (low <= 3) {
                i = read_modrm(bytes, avail, i, &in);
            } else if (low == 4) {
                i = imm_n(bytes, avail, i, &in, 1);
            } else {
                i = imm_n(bytes, avail, i, &in, in.os == 8 ? 4 : in.os);
            }
        }
    } else if (!two && (op == 0x63)) {
        in.op = CHRIS_OP_MOVSX;
        in.src_os = 4;
        if (!in.rex_w) {
            in.os = 4;
        }
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && op >= 0x50 && op <= 0x57) {
        in.op = CHRIS_OP_PUSH;
        in.os = os16 ? 2 : 8;
        in.reg_only_push = 1;
        in.rm = (op - 0x50) | (in.rex_b ? 8 : 0);
    } else if (!two && op >= 0x58 && op <= 0x5f) {
        in.op = CHRIS_OP_POP;
        in.os = os16 ? 2 : 8;
        in.reg_only_push = 1;
        in.rm = (op - 0x58) | (in.rex_b ? 8 : 0);
    } else if (!two && op == 0x68) {
        in.op = CHRIS_OP_PUSH;
        in.imm_src = 1;
        in.os = os16 ? 2 : 8;
        i = imm_n(bytes, avail, i, &in, in.os == 8 ? 4 : in.os);
    } else if (!two && op == 0x6a) {
        in.op = CHRIS_OP_PUSH;
        in.imm_src = 1;
        in.os = os16 ? 2 : 8;
        i = imm_n(bytes, avail, i, &in, 1);
    } else if (!two && op >= 0x70 && op <= 0x7f) {
        in.op = CHRIS_OP_JCC;
        in.cc = op & 15;
        i = imm_n(bytes, avail, i, &in, 1);
    } else if (!two && op >= 0x80 && op <= 0x83) {
        static const int map[8] = {CHRIS_ALU_ADD, CHRIS_ALU_OR,  CHRIS_ALU_ADC, CHRIS_ALU_SBB,
                                   CHRIS_ALU_AND, CHRIS_ALU_SUB, CHRIS_ALU_XOR, CHRIS_ALU_CMP};
        in.op = CHRIS_OP_ALU;
        in.form = CHRIS_FORM_RM_IMM;
        if (op == 0x80) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            in.alu = map[in.digit & 7];
            if (op == 0x81) {
                i = imm_n(bytes, avail, i, &in, in.os == 8 ? 4 : in.os);
            } else {
                i = imm_n(bytes, avail, i, &in, 1);
            }
        }
    } else if (!two && (op == 0x84 || op == 0x85)) {
        in.op = CHRIS_OP_ALU;
        in.alu = CHRIS_ALU_TEST;
        in.form = CHRIS_FORM_RM_REG;
        if (op == 0x84) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && (op == 0x86 || op == 0x87)) {
        in.op = CHRIS_OP_XCHG;
        if (op == 0x86) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && op >= 0x88 && op <= 0x8b) {
        int low = op & 3;
        in.op = CHRIS_OP_MOV;
        if (low == 0) {
            in.os = 1;
            in.form = CHRIS_FORM_RM_REG;
        } else if (low == 1) {
            in.form = CHRIS_FORM_RM_REG;
        } else if (low == 2) {
            in.os = 1;
            in.form = CHRIS_FORM_REG_RM;
        } else {
            in.form = CHRIS_FORM_REG_RM;
        }
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && (op == 0x8d)) {
        in.op = CHRIS_OP_LEA;
        in.form = CHRIS_FORM_REG_RM;
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && op == 0x8f) {
        in.op = CHRIS_OP_POP;
        in.os = os16 ? 2 : 8;
        i = read_modrm(bytes, avail, i, &in);
    } else if (!two && op >= 0x90 && op <= 0x97) {
        int r = (op - 0x90) | (in.rex_b ? 8 : 0);
        if (op == 0x90 && !in.rex_b) {
            in.op = CHRIS_OP_NOP;
        } else {
            in.op = CHRIS_OP_XCHG;
            in.mod = 3;
            in.reg = 0;
            in.rm = r;
            in.has_modrm = 1;
        }
    } else if (!two && op == 0x98) {
        in.op = CHRIS_OP_MOVSX;
        in.reg_only_push = 1;
        in.rm = 0;
        in.mod = 3;
        if (in.rex_w) {
            in.os = 8;
            in.src_os = 4;
        } else if (os16) {
            in.os = 2;
            in.src_os = 1;
        } else {
            in.os = 4;
            in.src_os = 2;
        }
    } else if (!two && op == 0x9c) {
        in.op = CHRIS_OP_PUSHF;
        in.os = os16 ? 2 : 8;
    } else if (!two && op == 0x9d) {
        in.op = CHRIS_OP_POPF;
        in.os = os16 ? 2 : 8;
    } else if (!two && (op == 0xa8 || op == 0xa9)) {
        in.op = CHRIS_OP_ALU;
        in.alu = CHRIS_ALU_TEST;
        in.form = CHRIS_FORM_ACC_IMM;
        in.acc_imm = 1;
        if (op == 0xa8) {
            in.os = 1;
        }
        i = imm_n(bytes, avail, i, &in, op == 0xa8 ? 1 : (in.os == 8 ? 4 : in.os));
    } else if (!two && op >= 0xb0 && op <= 0xb7) {
        in.op = CHRIS_OP_MOV;
        in.os = 1;
        in.reg_only_push = 1;
        in.rm = (op - 0xb0) | (in.rex ? (in.rex_b ? 8 : 0) : 0);
        if (!in.rex && (op - 0xb0) >= 4) {
            in.rm = op - 0xb0;
        }
        i = imm_n(bytes, avail, i, &in, 1);
    } else if (!two && op >= 0xb8 && op <= 0xbf) {
        in.op = CHRIS_OP_MOV;
        in.reg_only_push = 1;
        in.rm = (op - 0xb8) | (in.rex_b ? 8 : 0);
        i = imm_n(bytes, avail, i, &in, in.os == 2 ? 2 : in.os == 8 ? 8 : 4);
    } else if (!two && (op == 0xc0 || op == 0xc1 || op == 0xd0 || op == 0xd1 || op == 0xd2 || op == 0xd3)) {
        static const int sh[8] = {-1, -1, -1, -1, CHRIS_SH_SHL, CHRIS_SH_SHR, -1, CHRIS_SH_SAR};
        if (op == 0xc0 || op == 0xd0 || op == 0xd2) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            int kind = in.digit;
            if (kind == 0) {
                in.shift_kind = CHRIS_SH_ROL;
            } else if (kind == 1) {
                in.shift_kind = CHRIS_SH_ROR;
            } else if (kind < 8 && sh[kind] >= 0) {
                in.shift_kind = sh[kind];
            } else {
                in.op = CHRIS_OP_UNIMPL;
            }
            if (in.op != CHRIS_OP_UNIMPL) {
                in.op = CHRIS_OP_SHIFT;
            }
            if (op == 0xc0 || op == 0xc1) {
                i = imm_n(bytes, avail, i, &in, 1);
                in.shift_imm = 1;
            } else if (op == 0xd2 || op == 0xd3) {
                in.shift_cl = 1;
            } else {
                in.imm = 1;
            }
        }
    } else if (!two && op == 0xc2) {
        in.op = CHRIS_OP_RET;
        i = imm_n(bytes, avail, i, &in, 2);
    } else if (!two && op == 0xc3) {
        in.op = CHRIS_OP_RET;
    } else if (!two && (op == 0xc6 || op == 0xc7)) {
        if (op == 0xc6) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0 && in.digit == 0) {
            in.op = CHRIS_OP_MOV;
            in.form = CHRIS_FORM_RM_IMM;
            i = imm_n(bytes, avail, i, &in, op == 0xc6 ? 1 : (in.os == 8 ? 4 : in.os));
        } else if (i > 0) {
            in.op = CHRIS_OP_UD;
        }
    } else if (!two && op == 0xc9) {
        in.op = CHRIS_OP_LEAVE;
    } else if (!two && op == 0xcc) {
        in.op = CHRIS_OP_INT;
        in.vector = 3;
    } else if (!two && op == 0xcd) {
        in.op = CHRIS_OP_INT;
        i = imm_n(bytes, avail, i, &in, 1);
        if (i > 0) {
            in.vector = (int)in.imm;
        }
    } else if (!two && op == 0xcf) {
        in.op = CHRIS_OP_IRETQ;
    } else if (!two && (op == 0xe4 || op == 0xe5 || op == 0xe6 || op == 0xe7)) {
        in.op = (op == 0xe4 || op == 0xe5) ? CHRIS_OP_IN : CHRIS_OP_OUT;
        if (op == 0xe4 || op == 0xe6) {
            in.os = 1;
        }
        in.imm_src = 1;
        i = imm_n(bytes, avail, i, &in, 1);
    } else if (!two && (op == 0xec || op == 0xed || op == 0xee || op == 0xef)) {
        in.op = (op == 0xec || op == 0xed) ? CHRIS_OP_IN : CHRIS_OP_OUT;
        if (op == 0xec || op == 0xee) {
            in.os = 1;
        }
    } else if (!two && op == 0xe8) {
        in.op = CHRIS_OP_CALL;
        i = imm_n(bytes, avail, i, &in, 4);
    } else if (!two && op == 0xe9) {
        in.op = CHRIS_OP_JMP;
        i = imm_n(bytes, avail, i, &in, 4);
    } else if (!two && op == 0xeb) {
        in.op = CHRIS_OP_JMP;
        i = imm_n(bytes, avail, i, &in, 1);
    } else if (!two && op == 0xf4) {
        in.op = CHRIS_OP_HLT;
    } else if (!two && (op == 0xf6 || op == 0xf7)) {
        if (op == 0xf6) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            if (in.digit == 0) {
                in.op = CHRIS_OP_ALU;
                in.alu = CHRIS_ALU_TEST;
                in.form = CHRIS_FORM_RM_IMM;
                i = imm_n(bytes, avail, i, &in, op == 0xf6 ? 1 : (in.os == 8 ? 4 : in.os));
            } else if (in.digit == 2) {
                in.op = CHRIS_OP_UNARY;
                in.unary = CHRIS_UN_NOT;
            } else if (in.digit == 3) {
                in.op = CHRIS_OP_UNARY;
                in.unary = CHRIS_UN_NEG;
            } else if (in.digit == 4) {
                in.op = CHRIS_OP_MULDIV;
                in.muldiv = CHRIS_MD_MUL;
            } else if (in.digit == 5) {
                in.op = CHRIS_OP_MULDIV;
                in.muldiv = CHRIS_MD_IMUL;
            } else if (in.digit == 6) {
                in.op = CHRIS_OP_MULDIV;
                in.muldiv = CHRIS_MD_DIV;
            } else if (in.digit == 7) {
                in.op = CHRIS_OP_MULDIV;
                in.muldiv = CHRIS_MD_IDIV;
            } else {
                in.op = CHRIS_OP_UD;
            }
        }
    } else if (!two && op >= 0xf8 && op <= 0xfd) {
        static const int fl[6] = {CHRIS_FLAG_CLC, CHRIS_FLAG_STC, CHRIS_FLAG_CLI,
                                  CHRIS_FLAG_STI, CHRIS_FLAG_CLD, CHRIS_FLAG_STD};
        in.op = CHRIS_OP_FLAG;
        in.flag_op = fl[op - 0xf8];
    } else if (!two && (op == 0xfe || op == 0xff)) {
        if (op == 0xfe) {
            in.os = 1;
        }
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            if (in.digit == 0) {
                in.op = CHRIS_OP_UNARY;
                in.unary = CHRIS_UN_INC;
            } else if (in.digit == 1) {
                in.op = CHRIS_OP_UNARY;
                in.unary = CHRIS_UN_DEC;
            } else if (op == 0xff && in.digit == 2) {
                in.op = CHRIS_OP_CALL;
                in.os = 8;
            } else if (op == 0xff && in.digit == 4) {
                in.op = CHRIS_OP_JMP;
                in.os = 8;
            } else if (op == 0xff && in.digit == 6) {
                in.op = CHRIS_OP_PUSH;
                in.os = os16 ? 2 : 8;
            } else {
                in.op = CHRIS_OP_UD;
            }
        }
    } else if (two && op >= 0x40 && op <= 0x4f) {
        in.op = CHRIS_OP_CMOV;
        in.cc = op & 15;
        i = read_modrm(bytes, avail, i, &in);
    } else if (two && op == 0x01) {
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            if (in.mod == 3) {
                in.op = CHRIS_OP_UD;
            } else if (in.digit <= 3) {
                in.op = CHRIS_OP_DESC;
                in.desc_op = in.digit;
            } else {
                in.op = CHRIS_OP_UNIMPL;
            }
        }
    } else if (two && (op == 0x20 || op == 0x22)) {
        i = read_modrm(bytes, avail, i, &in);
        if (i > 0) {
            in.op = CHRIS_OP_MOVCR;
            in.cr_to_reg = op == 0x20;
            in.os = 8;
        }
    } else if (two && op == 0x30) {
        in.op = CHRIS_OP_WRMSR;
    } else if (two && op == 0x32) {
        in.op = CHRIS_OP_RDMSR;
    } else if (two && op == 0x1f) {
        in.op = CHRIS_OP_NOP;
        i = read_modrm(bytes, avail, i, &in);
    } else if (two && op >= 0x80 && op <= 0x8f) {
        in.op = CHRIS_OP_JCC;
        in.cc = op & 15;
        i = imm_n(bytes, avail, i, &in, 4);
    } else if (two && op >= 0x90 && op <= 0x9f) {
        in.op = CHRIS_OP_SETCC;
        in.cc = op & 15;
        in.os = 1;
        i = read_modrm(bytes, avail, i, &in);
    } else if (two && op == 0xa2) {
        in.op = CHRIS_OP_CPUID;
    } else if (two && op == 0xaf) {
        in.op = CHRIS_OP_MULDIV;
        in.muldiv = CHRIS_MD_IMUL2;
        i = read_modrm(bytes, avail, i, &in);
    } else if (two && (op == 0xb6 || op == 0xb7 || op == 0xbe || op == 0xbf)) {
        in.op = (op == 0xb6 || op == 0xb7) ? CHRIS_OP_MOVZX : CHRIS_OP_MOVSX;
        in.src_os = (op == 0xb6 || op == 0xbe) ? 1 : 2;
        i = read_modrm(bytes, avail, i, &in);
    } else {
        in.op = CHRIS_OP_UD;
    }
    if (i < 0) {
        return -1;
    }
    if (i > 15) {
        in.op = CHRIS_OP_UD;
        i = 15;
    }
    in.len = i;
    *out = in;
    return in.len;
}
