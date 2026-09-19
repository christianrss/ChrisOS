/* LEARN:SH16-13 */
#include "chrisasm.h"
#include <string.h>

#ifdef __freestanding__
#include "heap.h"
#endif

#define ASM_TEXT_MAX 65536u

#ifndef __freestanding__
static uint8_t g_text_host[ASM_TEXT_MAX];
#endif
static uint32_t g_text_len;
static uint8_t *g_text;

static int asm_fail(void) {
    return -1;
}

static void emit_u8(uint8_t b) {
    if (g_text_len >= ASM_TEXT_MAX) {
        return;
    }
    g_text[g_text_len++] = b;
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
    while (**p && !is_space(**p) && **p != ',' && **p != ':' && **p != ';') {
        if (n < cap - 1) {
            out[n++] = **p;
        }
        (*p)++;
    }
    out[n] = 0;
    return n > 0 ? 0 : -1;
}

static int parse_u64(const char **p, uint64_t *out) {
    char tok[32];
    uint64_t v = 0;
    int base = 10;
    int i = 0;

    if (parse_ident(p, tok, sizeof(tok)) != 0) {
        return -1;
    }
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        base = 16;
        i = 2;
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
            return -1;
        }
        if (d >= base) {
            return -1;
        }
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return 0;
}

static int reg_index(const char *r) {
    if (r[0] == 'r' && r[1] == 'a' && r[2] == 'x' && r[3] == 0) {
        return 0;
    }
    if (r[0] == 'r' && r[1] == 'b' && r[2] == 'x' && r[3] == 0) {
        return 3;
    }
    if (r[0] == 'r' && r[1] == 'c' && r[2] == 'x' && r[3] == 0) {
        return 1;
    }
    if (r[0] == 'r' && r[1] == 'd' && r[2] == 'x' && r[3] == 0) {
        return 2;
    }
    if (r[0] == 'r' && r[1] == 's' && r[2] == 'i' && r[3] == 0) {
        return 6;
    }
    if (r[0] == 'r' && r[1] == 'd' && r[2] == 'i' && r[3] == 0) {
        return 7;
    }
    return -1;
}

static int emit_mov_reg_imm64(int reg, uint64_t imm) {
    if (reg < 0) {
        return -1;
    }
    emit_u8(0x48);
    emit_u8((uint8_t)(0xb8 + reg));
    emit_u64(imm);
    return 0;
}

static int emit_mov_reg_reg(int dst, int src) {
    if (dst < 0 || src < 0) {
        return -1;
    }
    emit_u8(0x48);
    emit_u8(0x89);
    emit_u8((uint8_t)(0xc0 + (src << 3) + dst));
    return 0;
}

static int parse_line(const char *line, ChrisoImage *img) {
    const char *p = line;
    char op[16];
    char a[16];
    char b[16];
    uint64_t imm;

    skip_ws(&p);
    if (*p == 0 || *p == '#' || *p == ';') {
        return 0;
    }
    if (parse_ident(&p, op, sizeof(op)) != 0) {
        return 0;
    }
    if (op[0] == '.' && op[1] == 't' && op[2] == 'e' && op[3] == 'x' && op[4] == 't') {
        return 0;
    }
    skip_ws(&p);
    if (*p == ':') {
        if (img->nsym < CHRISO_SYM_MAX) {
            ChrisoSym *s = &img->sym[img->nsym++];
            memset(s, 0, sizeof(*s));
            strncpy(s->name, op, sizeof(s->name) - 1u);
            s->section = CHRISO_SEC_TEXT;
            s->offset = g_text_len;
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
        if (reg_index(a) == 0) {
            emit_u8(0x50);
            return 0;
        }
        return asm_fail();
    }
    if (op[0] == 'p' && op[1] == 'o' && op[2] == 'p') {
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        if (reg_index(a) == 0) {
            emit_u8(0x58);
            return 0;
        }
        return asm_fail();
    }
    if (op[0] == 'm' && op[1] == 'o' && op[2] == 'v') {
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        skip_ws(&p);
        if (*p == ',') {
            p++;
        }
        skip_ws(&p);
        {
            int ra = reg_index(a);
            if (ra < 0) {
                return asm_fail();
            }
            if (parse_u64(&p, &imm) == 0) {
                return emit_mov_reg_imm64(ra, imm);
            }
            if (parse_ident(&p, b, sizeof(b)) == 0) {
                int rb = reg_index(b);
                if (rb >= 0) {
                    return emit_mov_reg_reg(ra, rb);
                }
            }
        }
        return asm_fail();
    }
    if (op[0] == 'c' && op[1] == 'a' && op[2] == 'l' && op[3] == 'l') {
        skip_ws(&p);
        if (parse_ident(&p, a, sizeof(a)) != 0) {
            return asm_fail();
        }
        emit_u8(0xe8);
        emit_u32(0);
        if (img->nsym < CHRISO_SYM_MAX) {
            ChrisoSym *s = &img->sym[img->nsym++];
            memset(s, 0, sizeof(*s));
            strncpy(s->name, a, sizeof(s->name) - 1u);
            s->section = CHRISO_SEC_TEXT;
            s->offset = g_text_len - 4u;
        }
        return 0;
    }
    return 0;
}

int chrisasm_assemble(const char *src, ChrisoImage *out) {
    char line[256];
    int li = 0;

    if (!src || !out) {
        return -1;
    }
    chriso_init(out);
    g_text_len = 0;
#ifdef __freestanding__
    g_text = (uint8_t *)kmalloc(ASM_TEXT_MAX);
    if (!g_text) {
        return -1;
    }
#else
    g_text = g_text_host;
#endif
    out->sec[CHRISO_SEC_TEXT] = g_text;
    out->sec_size[CHRISO_SEC_TEXT] = 0;

    while (*src) {
        char c = *src++;
        if (c == '\n' || c == 0) {
            line[li] = 0;
            if (parse_line(line, out) != 0) {
#ifdef __freestanding__
                kfree(g_text);
                g_text = 0;
#endif
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
#ifdef __freestanding__
            kfree(g_text);
            g_text = 0;
#endif
            return -1;
        }
    }
    out->sec_size[CHRISO_SEC_TEXT] = g_text_len;
    return 0;
}
