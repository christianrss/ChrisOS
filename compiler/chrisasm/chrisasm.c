/* LEARN:SH16-13 */
#include "chrisasm.h"
#include <string.h>

#ifdef __freestanding__
#include "heap.h"
#else
#include <stdlib.h>
#endif

#define ASM_SEC_MAX 65536u

static uint8_t g_buf[3][ASM_SEC_MAX];
static uint32_t g_len[4];
static int g_cur;
static int g_overflow;
static int g_force_local;

static int asm_fail(void) {
    return -1;
}

static void emit_u8(uint8_t b) {
    if (g_cur < 0 || g_cur >= 3 || g_len[g_cur] >= ASM_SEC_MAX) {
        g_overflow = 1;
        return;
    }
    g_buf[g_cur][g_len[g_cur]++] = b;
}

static void emit_u32(uint32_t v) {
    emit_u8((uint8_t)(v & 0xffu));
    emit_u8((uint8_t)((v >> 8) & 0xffu));
    emit_u8((uint8_t)((v >> 16) & 0xffu));
    emit_u8((uint8_t)((v >> 24) & 0xffu));
}

static void emit_u64(uint64_t v) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        emit_u8((uint8_t)((v >> (i * 8u)) & 0xffu));
    }
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void skip_ws(const char **p) {
    while (**p && is_space(**p)) {
        (*p)++;
    }
}

static int parse_ident(const char **p, char *out, int cap) {
    int n = 0;
    skip_ws(p);
    if (cap < 1) {
        return -1;
    }
    while (**p && !is_space(**p) && **p != ',' && **p != ':' && **p != ';' &&
           **p != '[' && **p != ']' && **p != '+' && **p != '-') {
        if (n < cap - 1) {
            out[n++] = **p;
        }
        (*p)++;
    }
    out[n] = 0;
    return n > 0 ? 0 : -1;
}

static int parse_u64(const char **p, uint64_t *out) {
    const char *save = *p;
    char tok[32];
    uint64_t v = 0;
    int base = 10;
    int i = 0;

    if (parse_ident(p, tok, sizeof(tok)) != 0) {
        *p = save;
        return -1;
    }
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (!tok[i]) {
        *p = save;
        return -1;
    }
    for (; tok[i]; i++) {
        int d;
        char c = tok[i];
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            d = 10 + c - 'A';
        } else {
            *p = save;
            return -1;
        }
        if (d >= base) {
            *p = save;
            return -1;
        }
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return 0;
}

static int reg_index(const char *r) {
    static const char *names[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    int i;

    for (i = 0; i < 16; i++) {
        if (strcmp(r, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_sym(ChrisoImage *img, const char *name) {
    uint32_t i;

    for (i = 0; i < img->nsym; i++) {
        if (strcmp(img->sym[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int add_sym(ChrisoImage *img, const char *name, uint8_t binding,
                   uint8_t kind, uint32_t section, uint32_t offset) {
    int idx = find_sym(img, name);
    ChrisoSym *s;

    if (idx >= 0) {
        s = &img->sym[idx];
        if (binding != CHRISO_BIND_UNDEF) {
            if (s->binding != CHRISO_BIND_UNDEF) {
                return -1;
            }
            s->binding = binding;
            s->kind = kind;
            s->section = section;
            s->offset = offset;
        }
        return idx;
    }
    if (img->nsym >= CHRISO_SYM_MAX) {
        return -1;
    }
    s = &img->sym[img->nsym];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1u);
    s->binding = binding;
    s->kind = kind;
    s->section = section;
    s->offset = offset;
    return (int)img->nsym++;
}

static int add_reloc(ChrisoImage *img, uint32_t offset, uint32_t sym,
                     int32_t addend, uint32_t type) {
    ChrisoRel *r;

    if (img->nrel >= CHRISO_REL_MAX) {
        return -1;
    }
    r = &img->rel[img->nrel++];
    memset(r, 0, sizeof(*r));
    r->section = (uint32_t)g_cur;
    r->offset = offset;
    r->sym_index = sym;
    r->addend = addend;
    r->type = type;
    return 0;
}

static int emit_mov_reg_imm64(int reg, uint64_t imm) {
    if (reg < 0 || reg > 15) {
        return -1;
    }
    emit_u8((uint8_t)(0x48u | (reg >= 8 ? 1u : 0u)));
    emit_u8((uint8_t)(0xb8u + (reg & 7)));
    emit_u64(imm);
    return 0;
}

static int emit_mov_reg_reg(int dst, int src) {
    if (dst < 0 || src < 0 || dst > 15 || src > 15) {
        return -1;
    }
    emit_u8((uint8_t)(0x48u | (src >= 8 ? 4u : 0u) | (dst >= 8 ? 1u : 0u)));
    emit_u8(0x89);
    emit_u8((uint8_t)(0xc0u + ((src & 7) << 3) + (dst & 7)));
    return 0;
}

static int emit_push(int reg) {
    if (reg < 0 || reg > 15) {
        return -1;
    }
    if (reg >= 8) {
        emit_u8(0x41);
    }
    emit_u8((uint8_t)(0x50u + (reg & 7)));
    return 0;
}

static int emit_pop(int reg) {
    if (reg < 0 || reg > 15) {
        return -1;
    }
    if (reg >= 8) {
        emit_u8(0x41);
    }
    emit_u8((uint8_t)(0x58u + (reg & 7)));
    return 0;
}

static int reg_any(const char *r) {
    int i = reg_index(r);
    if (i >= 0) {
        return i;
    }
    if (strcmp(r, "al") == 0 || strcmp(r, "cl") == 0 || strcmp(r, "dl") == 0 ||
        strcmp(r, "bl") == 0) {
        return r[0] == 'a' ? 0 : r[0] == 'c' ? 1 : r[0] == 'd' ? 2 : 3;
    }
    return -1;
}

static int emit_rex(int w, int r, int b) {
    if (!w && r < 8 && b < 8) {
        return 0;
    }
    emit_u8((uint8_t)((w ? 0x48u : 0x40u) | (r >= 8 ? 4u : 0u) | (b >= 8 ? 1u : 0u)));
    return 0;
}

static int emit_disp_reloc(ChrisoImage *img, const char *sym) {
    int id = add_sym(img, sym, CHRISO_BIND_UNDEF, CHRISO_KIND_OBJECT, 0, 0);
    uint32_t at;

    if (id < 0) {
        return -1;
    }
    at = g_len[g_cur];
    emit_u32(0);
    return add_reloc(img, at, (uint32_t)id, -4, R_X86_64_PC32);
}

static int emit_mem_modrm(ChrisoImage *img, int reg, int rip, int base, int disp,
                          int has_disp, const char *sym) {
    int mod;
    int sib;
    int force8;

    if (rip) {
        emit_u8((uint8_t)(((reg & 7) << 3) | 5u));
        return emit_disp_reloc(img, sym);
    }
    sib = (base & 7) == 4;
    force8 = !has_disp && (base & 7) == 5;
    if (!has_disp && !force8) {
        mod = 0;
    } else if (disp >= -128 && disp <= 127) {
        mod = 1;
    } else {
        mod = 2;
    }
    emit_u8((uint8_t)((mod << 6) | ((reg & 7) << 3) | (base & 7)));
    if (sib) {
        emit_u8((uint8_t)(0x20u | (base & 7)));
    }
    if (mod == 1) {
        emit_u8((uint8_t)disp);
    } else if (mod == 2) {
        emit_u32((uint32_t)disp);
    }
    return 0;
}

static int emit_call_or_jmp(ChrisoImage *img, const char *sym, int cond) {
    int id = add_sym(img, sym, CHRISO_BIND_UNDEF, CHRISO_KIND_FUNC, 0, 0);
    uint32_t at;

    if (id < 0) {
        return -1;
    }
    if (cond == -1) {
        emit_u8(0xe8);
    } else if (cond == -2) {
        emit_u8(0xe9);
    } else {
        emit_u8(0x0f);
        emit_u8((uint8_t)cond);
    }
    at = g_len[g_cur];
    emit_u32(0);
    return add_reloc(img, at, (uint32_t)id, -4, R_X86_64_PLT32);
}

static int parse_mem(const char **p, int *rip, int *base, int *disp, int *has_disp,
                     char *sym, int symcap) {
    skip_ws(p);
    if (**p != '[') {
        return -1;
    }
    (*p)++;
    skip_ws(p);
    if ((*p)[0] == 'r' && (*p)[1] == 'e' && (*p)[2] == 'l' &&
        ((*p)[3] == ' ' || (*p)[3] == '\t')) {
        *rip = 1;
        *base = 0;
        *disp = 0;
        *has_disp = 0;
        (*p) += 3;
        if (parse_ident(p, sym, symcap) != 0) {
            return -1;
        }
    } else {
        char name[32];
        *rip = 0;
        if (parse_ident(p, name, sizeof(name)) != 0) {
            return -1;
        }
        *base = reg_index(name);
        if (*base < 0) {
            return -1;
        }
        skip_ws(p);
        if (**p == '+' || **p == '-') {
            int sign = **p == '-' ? -1 : 1;
            uint64_t v;
            (*p)++;
            if (parse_u64(p, &v) != 0) {
                return -1;
            }
            *disp = sign * (int)v;
            *has_disp = 1;
        } else {
            *disp = 0;
            *has_disp = 0;
        }
    }
    skip_ws(p);
    if (**p != ']') {
        return -1;
    }
    (*p)++;
    return 0;
}

static int emit_rm(ChrisoImage *img, int w, int opc, int reg, int rip, int base,
                   int disp, int has_disp, const char *sym, int is_reg, int rm) {
    emit_rex(w, reg, rip ? 0 : (is_reg ? rm : base));
    emit_u8((uint8_t)opc);
    if (is_reg) {
        emit_u8((uint8_t)(0xc0u | ((reg & 7) << 3) | (rm & 7)));
        return 0;
    }
    return emit_mem_modrm(img, reg, rip, base, disp, has_disp, sym);
}

static int sec_of(const char *op) {
    if (strcmp(op, ".text") == 0) {
        return CHRISO_SEC_TEXT;
    }
    if (strcmp(op, ".rodata") == 0) {
        return CHRISO_SEC_RODATA;
    }
    if (strcmp(op, ".data") == 0) {
        return CHRISO_SEC_DATA;
    }
    if (strcmp(op, ".bss") == 0) {
        return CHRISO_SEC_BSS;
    }
    return -1;
}

static int emit_zeros(uint32_t n) {
    uint32_t i;
    if (g_cur == CHRISO_SEC_BSS) {
        if (g_len[CHRISO_SEC_BSS] + n < g_len[CHRISO_SEC_BSS]) {
            return -1;
        }
        g_len[CHRISO_SEC_BSS] += n;
        return 0;
    }
    for (i = 0; i < n; i++) {
        emit_u8(0);
    }
    return g_overflow ? -1 : 0;
}

static int parse_string(const char **p, char *out, int cap, int *nlen) {
    int n = 0;
    skip_ws(p);
    if (**p != '"') {
        return -1;
    }
    (*p)++;
    while (**p && **p != '"') {
        char c = **p;
        (*p)++;
        if (c == '\\') {
            c = **p;
            if (!c) {
                return -1;
            }
            (*p)++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == '0') {
                c = 0;
            }
        }
        if (n < cap) {
            out[n] = c;
        }
        n++;
    }
    if (**p != '"') {
        return -1;
    }
    (*p)++;
    *nlen = n;
    return 0;
}

static int jcc_of(const char *op) {
    if (strcmp(op, "jmp") == 0) {
        return -2;
    }
    if (strcmp(op, "je") == 0 || strcmp(op, "jz") == 0) {
        return 0x84;
    }
    if (strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0) {
        return 0x85;
    }
    if (strcmp(op, "jb") == 0) {
        return 0x82;
    }
    if (strcmp(op, "jae") == 0) {
        return 0x83;
    }
    if (strcmp(op, "jbe") == 0) {
        return 0x86;
    }
    if (strcmp(op, "ja") == 0) {
        return 0x87;
    }
    if (strcmp(op, "jl") == 0) {
        return 0x8c;
    }
    if (strcmp(op, "jge") == 0) {
        return 0x8d;
    }
    if (strcmp(op, "jle") == 0) {
        return 0x8e;
    }
    if (strcmp(op, "jg") == 0) {
        return 0x8f;
    }
    return -1;
}

static int parse_line(const char *line, ChrisoImage *img) {
    const char *p = line;
    char op[64];
    char a[64];
    char b[64];
    uint64_t imm;
    int sec;

    skip_ws(&p);
    if (*p == 0 || *p == '#' || *p == ';') {
        return 0;
    }
    if (parse_ident(&p, op, sizeof(op)) != 0) {
        return 0;
    }
    sec = sec_of(op);
    if (sec >= 0) {
        g_cur = sec;
        return 0;
    }
    if (strcmp(op, ".global") == 0 || strcmp(op, "global") == 0) {
        return 0;
    }
    if (strcmp(op, ".local") == 0) {
        g_force_local = 1;
        return 0;
    }
    if (strcmp(op, ".extern") == 0 || strcmp(op, "extern") == 0) {
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        return add_sym(img, a, CHRISO_BIND_UNDEF, CHRISO_KIND_FUNC, 0, 0) < 0
                   ? asm_fail()
                   : 0;
    }
    if (strcmp(op, ".zero") == 0 || strcmp(op, ".skip") == 0) {
        if (parse_u64(&p, &imm) != 0) {
            return asm_fail();
        }
        return emit_zeros((uint32_t)imm);
    }
    if (strcmp(op, ".asciz") == 0 || strcmp(op, ".ascii") == 0) {
        char lit[256];
        int n = 0;
        int i;
        int z = strcmp(op, ".asciz") == 0;
        if (g_cur == CHRISO_SEC_BSS || parse_string(&p, lit, 255, &n) != 0) {
            return asm_fail();
        }
        for (i = 0; i < n && i < 255; i++) {
            emit_u8((uint8_t)lit[i]);
        }
        if (z) {
            emit_u8(0);
        }
        return g_overflow ? asm_fail() : 0;
    }
    if (strcmp(op, ".byte") == 0) {
        if (parse_u64(&p, &imm) != 0 || g_cur == CHRISO_SEC_BSS) {
            return asm_fail();
        }
        emit_u8((uint8_t)imm);
        return 0;
    }
    if (strcmp(op, ".quad") == 0) {
        if (parse_u64(&p, &imm) != 0 || g_cur == CHRISO_SEC_BSS) {
            return asm_fail();
        }
        emit_u64(imm);
        return 0;
    }
    skip_ws(&p);
    if (*p == ':') {
        uint8_t bind = (uint8_t)((g_force_local || (op[0] == '.' && op[1] == 'L'))
                                     ? CHRISO_BIND_LOCAL
                                     : CHRISO_BIND_GLOBAL);
        uint8_t kind = (uint8_t)(g_cur == CHRISO_SEC_TEXT ? CHRISO_KIND_FUNC
                                                         : CHRISO_KIND_OBJECT);
        g_force_local = 0;
        if (add_sym(img, op, bind, kind, (uint32_t)g_cur, g_len[g_cur]) < 0) {
            return asm_fail();
        }
        return 0;
    }
    if (op[0] == 'r' && op[1] == 'e' && op[2] == 't' && op[3] == 0) {
        emit_u8(0xc3);
        return 0;
    }
    if (op[0] == 's' && op[1] == 'y' && op[2] == 's' && op[3] == 'c' &&
        op[4] == 'a' && op[5] == 'l' && op[6] == 'l' && op[7] == 0) {
        emit_u8(0x0f);
        emit_u8(0x05);
        return 0;
    }
    if (op[0] == 'p' && op[1] == 'u' && op[2] == 's' && op[3] == 'h') {
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        if (emit_push(reg_index(a)) != 0) {
            return asm_fail();
        }
        return 0;
    }
    if (op[0] == 'p' && op[1] == 'o' && op[2] == 'p') {
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        if (emit_pop(reg_index(a)) != 0) {
            return asm_fail();
        }
        return 0;
    }
    if (strcmp(op, "call") == 0 || jcc_of(op) != -1) {
        int cc = strcmp(op, "call") == 0 ? -3 : jcc_of(op);
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        if (cc == -3) {
            return emit_call_or_jmp(img, a, -1) != 0 ? asm_fail() : 0;
        }
        if (cc == -2) {
            return emit_call_or_jmp(img, a, -2) != 0 ? asm_fail() : 0;
        }
        return emit_call_or_jmp(img, a, cc) != 0 ? asm_fail() : 0;
    }
    if (strcmp(op, "mov") == 0 || strcmp(op, "lea") == 0 || strcmp(op, "movzx") == 0 ||
        strcmp(op, "cmp") == 0 || strcmp(op, "test") == 0 || strcmp(op, "add") == 0 ||
        strcmp(op, "sub") == 0 || strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
        strcmp(op, "xor") == 0 || strcmp(op, "shl") == 0 || strcmp(op, "shr") == 0 ||
        strcmp(op, "imul") == 0 || strcmp(op, "div") == 0) {
        int size = 8;
        int rip = 0;
        int base = 0;
        int disp = 0;
        int has_disp = 0;
        int dst_reg;
        int src_reg = -1;
        int src_imm = 0;
        int src_mem = 0;
        int dst_mem = 0;
        int dst_rip = 0;
        int dst_base = 0;
        int dst_disp = 0;
        int dst_has = 0;
        char dsym[64];
        char ssym[64];
        const char *save;

        skip_ws(&p);
        if (strcmp(p, "") == 0) {
            return asm_fail();
        }
        if (p[0] == 'b' && p[1] == 'y' && p[2] == 't' && p[3] == 'e' &&
            (p[4] == ' ' || p[4] == '\t' || p[4] == '[')) {
            size = 1;
            p += 4;
            skip_ws(&p);
        }
        save = p;
        if (*p == '[') {
            dst_mem = 1;
            if (parse_mem(&p, &dst_rip, &dst_base, &dst_disp, &dst_has, dsym,
                          (int)sizeof(dsym)) != 0) {
                return asm_fail();
            }
            dst_reg = 0;
        } else {
            if (parse_ident(&p, a, sizeof(a)) != 0) {
                return asm_fail();
            }
            dst_reg = reg_any(a);
            if (dst_reg < 0) {
                p = save;
                return asm_fail();
            }
        }
        skip_ws(&p);
        if (strcmp(op, "div") == 0) {
            if (dst_mem) {
                return asm_fail();
            }
            return emit_rm(img, 1, 0xf7, 6, 0, 0, 0, 0, 0, 1, dst_reg) != 0 ? asm_fail()
                                                                             : 0;
        }
        if (*p == ',') {
            p++;
        }
        skip_ws(&p);
        if (p[0] == 'b' && p[1] == 'y' && p[2] == 't' && p[3] == 'e' &&
            (p[4] == ' ' || p[4] == '\t' || p[4] == '[')) {
            size = 1;
            p += 4;
            skip_ws(&p);
        }
        if (*p == '[') {
            src_mem = 1;
            if (parse_mem(&p, &rip, &base, &disp, &has_disp, ssym, (int)sizeof(ssym)) != 0) {
                return asm_fail();
            }
        } else if (parse_u64(&p, &imm) == 0) {
            src_imm = 1;
        } else if (parse_ident(&p, b, sizeof(b)) == 0) {
            src_reg = reg_any(b);
            if (src_reg < 0) {
                return asm_fail();
            }
        } else {
            return asm_fail();
        }
        if (strcmp(op, "mov") == 0 && !dst_mem && src_imm) {
            return emit_mov_reg_imm64(dst_reg, imm);
        }
        if (strcmp(op, "mov") == 0 && !dst_mem && !src_mem && src_reg >= 0 && size == 8) {
            return emit_mov_reg_reg(dst_reg, src_reg);
        }
        if (strcmp(op, "lea") == 0) {
            if (dst_mem || !src_mem) {
                return asm_fail();
            }
            emit_rex(1, dst_reg, rip ? 0 : base);
            emit_u8(0x8d);
            return emit_mem_modrm(img, dst_reg, rip, base, disp, has_disp, ssym) != 0
                       ? asm_fail()
                       : 0;
        }
        if (strcmp(op, "movzx") == 0) {
            if (dst_mem || !src_mem) {
                return asm_fail();
            }
            emit_rex(1, dst_reg, rip ? 0 : base);
            emit_u8(0x0f);
            emit_u8(0xb6);
            return emit_mem_modrm(img, dst_reg, rip, base, disp, has_disp, ssym) != 0
                       ? asm_fail()
                       : 0;
        }
        if (strcmp(op, "mov") == 0 && dst_mem && src_reg >= 0) {
            int opc = size == 1 ? 0x88 : 0x89;
            int w = size == 1 ? 0 : 1;
            return emit_rm(img, w, opc, src_reg, dst_rip, dst_base, dst_disp, dst_has, dsym,
                           0, 0) != 0
                       ? asm_fail()
                       : 0;
        }
        if (strcmp(op, "mov") == 0 && !dst_mem && src_mem) {
            emit_rex(1, dst_reg, rip ? 0 : base);
            emit_u8(0x8b);
            return emit_mem_modrm(img, dst_reg, rip, base, disp, has_disp, ssym) != 0
                       ? asm_fail()
                       : 0;
        }
        if ((strcmp(op, "shl") == 0 || strcmp(op, "shr") == 0) && !dst_mem) {
            int ext = strcmp(op, "shl") == 0 ? 4 : 5;
            if (src_reg == 1) {
                return emit_rm(img, 1, 0xd3, ext, 0, 0, 0, 0, 0, 1, dst_reg) != 0 ? asm_fail()
                                                                                   : 0;
            }
            if (!src_imm) {
                return asm_fail();
            }
            emit_rm(img, 1, 0xc1, ext, 0, 0, 0, 0, 0, 1, dst_reg);
            emit_u8((uint8_t)imm);
            return 0;
        }
        if (strcmp(op, "imul") == 0 && !dst_mem && src_reg >= 0) {
            emit_rex(1, dst_reg, src_reg);
            emit_u8(0x0f);
            emit_u8(0xaf);
            emit_u8((uint8_t)(0xc0u | ((dst_reg & 7) << 3) | (src_reg & 7)));
            return 0;
        }
        if (!dst_mem && (src_reg >= 0 || src_imm)) {
            int ext = 0;
            int regopc = 0;
            if (strcmp(op, "add") == 0) {
                ext = 0;
                regopc = 0x01;
            } else if (strcmp(op, "or") == 0) {
                ext = 1;
                regopc = 0x09;
            } else if (strcmp(op, "and") == 0) {
                ext = 4;
                regopc = 0x21;
            } else if (strcmp(op, "sub") == 0) {
                ext = 5;
                regopc = 0x29;
            } else if (strcmp(op, "xor") == 0) {
                ext = 6;
                regopc = 0x31;
            } else if (strcmp(op, "cmp") == 0) {
                ext = 7;
                regopc = 0x39;
            } else if (strcmp(op, "test") == 0) {
                ext = src_reg;
                regopc = 0x85;
            } else {
                return asm_fail();
            }
            if (src_reg >= 0) {
                return emit_rm(img, 1, regopc, strcmp(op, "test") == 0 ? src_reg : src_reg,
                               0, 0, 0, 0, 0, 1, dst_reg) != 0
                           ? asm_fail()
                           : 0;
            }
            if (strcmp(op, "test") == 0) {
                return asm_fail();
            }
            emit_rm(img, 1, 0x81, ext, 0, 0, 0, 0, 0, 1, dst_reg);
            emit_u32((uint32_t)imm);
            return 0;
        }
        return asm_fail();
    }
    return asm_fail();
}

static int publish_secs(ChrisoImage *out) {
    int s;

    for (s = 0; s < 3; s++) {
        out->sec_size[s] = g_len[s];
        out->sec[s] = 0;
        if (g_len[s] == 0u) {
            continue;
        }
#ifdef __freestanding__
        out->sec[s] = (uint8_t *)kmalloc(g_len[s]);
#else
        out->sec[s] = (uint8_t *)malloc(g_len[s]);
#endif
        if (!out->sec[s]) {
            return -1;
        }
        memcpy(out->sec[s], g_buf[s], g_len[s]);
    }
    out->sec[CHRISO_SEC_BSS] = 0;
    out->sec_size[CHRISO_SEC_BSS] = g_len[CHRISO_SEC_BSS];
    return 0;
}

int chrisasm_assemble(const char *src, ChrisoImage *out) {
    char line[512];
    int li = 0;
    int i;

    if (!src || !out) {
        return -1;
    }
    chriso_init(out);
    g_overflow = 0;
    g_force_local = 0;
    g_cur = CHRISO_SEC_TEXT;
    for (i = 0; i < 4; i++) {
        g_len[i] = 0;
    }
    while (*src) {
        char c = *src++;
        if (c == '\n' || c == 0) {
            line[li] = 0;
            if (parse_line(line, out) != 0) {
                return -1;
            }
            li = 0;
            if (c == 0) {
                break;
            }
            continue;
        }
        if (li < (int)sizeof(line) - 1) {
            line[li++] = c;
        }
    }
    if (li > 0) {
        line[li] = 0;
        if (parse_line(line, out) != 0) {
            return -1;
        }
    }
    if (g_overflow) {
        return -1;
    }
    return publish_secs(out);
}
