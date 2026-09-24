/* LEARN:SH16-15 — subset C → asm → ChrisO */
#include "kcc.h"
#include "chrisasm.h"
#include <string.h>

#define KCC_ASM_MAX 32768u

static char g_asm[KCC_ASM_MAX];
static int g_asm_len;
static int g_asm_overflow;
static KccDiag g_diag;

static void asm_puts(const char *s) {
    while (*s) {
        if (g_asm_len >= (int)KCC_ASM_MAX - 1) {
            g_asm_overflow = 1;
            return;
        }
        g_asm[g_asm_len++] = *s++;
    }
}

static void asm_line(const char *s) {
    asm_puts(s);
    asm_puts("\n");
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') {
        (*p)++;
    }
}

static int skip_block_comment(const char **p) {
    if ((*p)[0] != '/' || (*p)[1] != '*') {
        return 0;
    }
    (*p) += 2;
    while (**p) {
        if ((*p)[0] == '*' && (*p)[1] == '/') {
            (*p) += 2;
            return 0;
        }
        (*p)++;
    }
    return -1;
}

static int match_word(const char **p, const char *w) {
    const char *s = *p;
    int i = 0;
    skip_ws(p);
    while (w[i]) {
        if ((*p)[i] != w[i]) {
            *p = s;
            return 0;
        }
        i++;
    }
    if (is_ident_start((*p)[i])) {
        *p = s;
        return 0;
    }
    *p += i;
    return 1;
}

static int emit_outb_call(uint16_t port, uint8_t val) {
    asm_line("mov rax, 0");
    {
        char buf[80];
        int n = 0;
        const char *pre = "mov rdi, ";
        while (pre[n]) {
            buf[n] = pre[n];
            n++;
        }
        {
            uint32_t v = port;
            char tmp[16];
            int tn = 0;
            if (v == 0) {
                tmp[tn++] = '0';
            }
            while (v > 0 && tn < 14) {
                tmp[tn++] = (char)('0' + (v % 10u));
                v /= 10u;
            }
            while (tn > 0) {
                buf[n++] = tmp[--tn];
            }
        }
        buf[n] = 0;
        asm_line(buf);
    }
    {
        char buf[80];
        int n = 0;
        const char *pre = "mov rsi, ";
        while (pre[n]) {
            buf[n] = pre[n];
            n++;
        }
        {
            uint32_t v = val;
            char tmp[16];
            int tn = 0;
            if (v == 0) {
                tmp[tn++] = '0';
            }
            while (v > 0 && tn < 14) {
                tmp[tn++] = (char)('0' + (v % 10u));
                v /= 10u;
            }
            while (tn > 0) {
                buf[n++] = tmp[--tn];
            }
        }
        buf[n] = 0;
        asm_line(buf);
    }
    asm_line("call outb");
    return 0;
}

static int parse_outb_line(const char *line) {
    const char *p = line;
    uint16_t port = 0;
    uint8_t val = 0;
    int saw_port = 0;
    int saw_val = 0;

    skip_ws(&p);
    if (!match_word(&p, "outb")) {
        return 0;
    }
    skip_ws(&p);
    if (*p == '(') {
        p++;
    }
    skip_ws(&p);
    while (*p >= '0' && *p <= '9') {
        port = (uint16_t)(port * 10u + (uint16_t)(*p - '0'));
        saw_port = 1;
        p++;
    }
    skip_ws(&p);
    if (*p == '+') {
        p++;
        skip_ws(&p);
        while (*p >= '0' && *p <= '9') {
            port = (uint16_t)(port + (uint16_t)(*p - '0'));
            p++;
        }
    }
    skip_ws(&p);
    if (*p == ',') {
        p++;
    }
    skip_ws(&p);
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        while ((*p >= '0' && *p <= '9') ||
               (*p >= 'a' && *p <= 'f') ||
               (*p >= 'A' && *p <= 'F')) {
            int d;
            if (*p >= '0' && *p <= '9') {
                d = *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                d = 10 + *p - 'a';
            } else {
                d = 10 + *p - 'A';
            }
            val = (uint8_t)((val << 4) + (uint8_t)d);
            saw_val = 1;
            p++;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            val = (uint8_t)(val * 10u + (uint8_t)(*p - '0'));
            saw_val = 1;
            p++;
        }
    }
    if (!(saw_port && saw_val)) {
        return -1;
    }
    skip_ws(&p);
    if (*p == ')') {
        p++;
        skip_ws(&p);
    }
    if (*p == ';') {
        p++;
        skip_ws(&p);
    }
    if (*p) {
        return -1;
    }
    return emit_outb_call(port, val) == 0 ? 1 : -1;
}

static int trailing_only(const char *p) {
    skip_ws(&p);
    if (*p == ';') {
        p++;
        skip_ws(&p);
    }
    return *p == 0;
}

static int parse_return_int(const char *line) {
    const char *p = line;
    uint64_t v = 0;
    int saw = 0;

    skip_ws(&p);
    if (!match_word(&p, "return")) {
        return 0;
    }
    skip_ws(&p);
    if (*p == 0 || *p == ';') {
        if (!trailing_only(p)) {
            return -1;
        }
        asm_line("ret");
        return 1;
    }
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        while ((*p >= '0' && *p <= '9') ||
               (*p >= 'a' && *p <= 'f') ||
               (*p >= 'A' && *p <= 'F')) {
            int d;
            if (*p >= '0' && *p <= '9') {
                d = *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                d = 10 + *p - 'a';
            } else {
                d = 10 + *p - 'A';
            }
            v = (v << 4) + (uint64_t)d;
            saw = 1;
            p++;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            v = v * 10ull + (uint64_t)(*p - '0');
            saw = 1;
            p++;
        }
    }
    if (!saw || !trailing_only(p)) {
        return -1;
    }
    {
        char buf[64];
        int n = 0;
        const char *pre = "mov rax, ";
        while (pre[n]) {
            buf[n] = pre[n];
            n++;
        }
        {
            char tmp[24];
            int tn = 0;
            uint64_t x = v;
            if (x == 0) {
                tmp[tn++] = '0';
            }
            while (x > 0 && tn < 22) {
                tmp[tn++] = (char)('0' + (x % 10ull));
                x /= 10ull;
            }
            while (tn > 0) {
                buf[n++] = tmp[--tn];
            }
        }
        buf[n] = 0;
        asm_line(buf);
    }
    asm_line("ret");
    return 1;
}

#define KCC_LINE_MAX 512

static void diag_clear(void) {
    memset(&g_diag, 0, sizeof(g_diag));
}

static int diag_error(const char *file, int line, int column, const char *msg) {
    unsigned i;

    diag_clear();
    if (file) {
        for (i = 0; file[i] && i + 1u < sizeof(g_diag.file); i++) {
            g_diag.file[i] = file[i];
        }
    }
    g_diag.line = line;
    g_diag.column = column;
    g_diag.severity = 1;
    for (i = 0; msg[i] && i + 1u < sizeof(g_diag.message); i++) {
        g_diag.message[i] = msg[i];
    }
    return -1;
}

const KccDiag *kcc_last_error(void) {
    return &g_diag;
}

static int first_column(const char *line) {
    int column = 1;
    while (*line == ' ' || *line == '\t') {
        column++;
        line++;
    }
    return column;
}

static int line_is_blank(const char *line) {
    skip_ws(&line);
    return *line == 0;
}

static int line_is_slash_comment(const char *line) {
    skip_ws(&line);
    return line[0] == '/' && line[1] == '/';
}

static int line_is_block_comment(const char *line) {
    skip_ws(&line);
    return line[0] == '/' && line[1] == '*';
}

static int comment_ends_here(const char *line) {
    while (*line) {
        if (line[0] == '*' && line[1] == '/') {
            return 1;
        }
        line++;
    }
    return 0;
}

static int is_type_word(const char **line) {
    return match_word(line, "void") || match_word(line, "bool") ||
           match_word(line, "int") || match_word(line, "uint8_t") ||
           match_word(line, "uint16_t") || match_word(line, "uint32_t") ||
           match_word(line, "uint64_t");
}

static int emit_function(const char *line, int *is_kstart) {
    const char *q = line;
    const char *paren;
    char name[64];
    int ni = 0;
    char lbl[72];
    int i = 0;

    skip_ws(&q);
    while (*q && *q != '(' && *q != ' ' && *q != '\t' && *q != '*') {
        if (is_ident_start(*q) || (*q >= '0' && *q <= '9')) {
            if (ni < 63) {
                name[ni++] = *q;
            }
        }
        q++;
    }
    name[ni] = 0;
    paren = q;
    while (*paren && *paren != '(' && *paren != '\n') {
        paren++;
    }
    if (*paren != '(' || ni <= 0) {
        return -1;
    }
    *is_kstart = (name[0] == 'k' && name[1] == 's' && name[2] == 't' &&
                  name[3] == 'a' && name[4] == 'r' && name[5] == 't' &&
                  name[6] == 0);
    while (name[i] && i < 62) {
        lbl[i] = name[i];
        i++;
    }
    lbl[i++] = ':';
    lbl[i] = 0;
    asm_line(lbl);
    return 0;
}

int kcc_compile_named(const char *file, const char *src, ChrisoImage *out) {
    const char *p;
    int line_no = 1;
    int in_fn = 0;
    int fn_is_kstart = 0;

    diag_clear();
    if (!src || !out) {
        return diag_error(file, 0, 0, "missing source");
    }
    g_asm_len = 0;
    g_asm_overflow = 0;
    asm_line(".text");
    p = src;
    while (*p) {
        const char *line_start = p;
        char buf[KCC_LINE_MAX];
        int len = 0;
        int this_line = line_no;
        const char *probe;
        int rc;

        while (*p && *p != '\n') {
            p++;
        }
        len = (int)(p - line_start);
        if (*p == '\n') {
            p++;
            line_no++;
        }
        if (len >= KCC_LINE_MAX) {
            return diag_error(file, this_line, 1, "line is too long");
        }
        memcpy(buf, line_start, (size_t)len);
        buf[len] = 0;
        if (line_is_blank(buf) || line_is_slash_comment(buf)) {
            continue;
        }
        if (line_is_block_comment(buf)) {
            if (!comment_ends_here(buf)) {
                const char *c = line_start;
                skip_ws(&c);
                if (skip_block_comment(&c) != 0) {
                    return diag_error(file, this_line, first_column(buf),
                                      "unclosed block comment");
                }
                while (p < c) {
                    if (*p == '\n') {
                        line_no++;
                    }
                    p++;
                }
            }
            continue;
        }
        probe = buf;
        skip_ws(&probe);
        if (*probe == '#') {
            return diag_error(file, this_line, first_column(buf),
                              "preprocessor directive");
        }
        probe = buf;
        if (match_word(&probe, "static")) {
            return diag_error(file, this_line, first_column(buf),
                              "static is outside the level-0 subset");
        }
        probe = buf;
        if (!in_fn && is_type_word(&probe)) {
            if (emit_function(probe, &fn_is_kstart) != 0) {
                return diag_error(file, this_line, first_column(buf),
                                  "declaration is outside the level-0 subset");
            }
            in_fn = 1;
            continue;
        }
        if (!in_fn) {
            return diag_error(file, this_line, first_column(buf),
                              "declaration is outside the level-0 subset");
        }
        probe = buf;
        skip_ws(&probe);
        if (*probe == '{') {
            probe++;
            if (!trailing_only(probe)) {
                return diag_error(file, this_line, first_column(buf),
                                  "statement is outside the level-0 subset");
            }
            continue;
        }
        if (*probe == '}') {
            probe++;
            if (!trailing_only(probe)) {
                return diag_error(file, this_line, first_column(buf),
                                  "statement is outside the level-0 subset");
            }
            if (fn_is_kstart) {
                asm_line("mov rax, 0");
                asm_line("ret");
            }
            in_fn = 0;
            fn_is_kstart = 0;
            continue;
        }
        probe = buf;
        if (is_type_word(&probe)) {
            return diag_error(file, this_line, first_column(buf),
                              "declaration is outside the level-0 subset");
        }
        rc = parse_outb_line(buf);
        if (rc < 0) {
            return diag_error(file, this_line, first_column(buf),
                              "outb arguments are not integer literals");
        }
        if (rc > 0) {
            continue;
        }
        rc = parse_return_int(buf);
        if (rc < 0) {
            return diag_error(file, this_line, first_column(buf),
                              "return value is not an integer literal");
        }
        if (rc > 0) {
            continue;
        }
        return diag_error(file, this_line, first_column(buf),
                          "statement is outside the level-0 subset");
    }
    if (in_fn) {
        return diag_error(file, line_no, 1, "unclosed function");
    }
    if (g_asm_overflow) {
        return diag_error(file, 1, 1, "assembly buffer is full");
    }
    g_asm[g_asm_len] = 0;
    if (chrisasm_assemble(g_asm, out) != 0) {
        return diag_error(file, 1, 1, "assembler rejected the translation");
    }
    return 0;
}

int kcc_compile_source(const char *src, ChrisoImage *out) {
    return kcc_compile_named(0, src, out);
}
