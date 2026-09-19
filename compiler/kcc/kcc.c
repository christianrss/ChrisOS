/* LEARN:SH16-15 — subset C → asm → ChrisO */
#include "kcc.h"
#include "chrisasm.h"
#include <string.h>

#define KCC_ASM_MAX 32768u

static char g_asm[KCC_ASM_MAX];
static int g_asm_len;

static void asm_puts(const char *s) {
    while (*s && g_asm_len < (int)KCC_ASM_MAX - 1) {
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
    if (saw_port && saw_val) {
        return emit_outb_call(port, val);
    }
    return 0;
}

static int parse_return_int(const char *line) {
    const char *p = line;
    uint64_t v = 0;

    skip_ws(&p);
    if (!match_word(&p, "return")) {
        return 0;
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
            v = (v << 4) + (uint64_t)d;
            p++;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            v = v * 10ull + (uint64_t)(*p - '0');
            p++;
        }
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

int kcc_compile_source(const char *src, ChrisoImage *out) {
    const char *p;
    int in_fn = 0;
    int fn_is_kstart = 0;

    if (!src || !out) {
        return -1;
    }
    g_asm_len = 0;
    asm_line(".text");
    p = src;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        if (line[0] == '#') {
            continue;
        }
        if (line[0] == '/' && line[1] == '/') {
            continue;
        }
        if (line[0] == '/' && line[1] == '*') {
            p = line;
            if (skip_block_comment(&p) != 0) {
                return -1;
            }
            continue;
        }
        if (match_word(&line, "#include") || match_word(&line, "#define")) {
            continue;
        }
        if (match_word(&line, "static")) {
            continue;
        }
        if (match_word(&line, "void") || match_word(&line, "bool") ||
            match_word(&line, "int") || match_word(&line, "uint8_t") ||
            match_word(&line, "uint16_t") || match_word(&line, "uint32_t") ||
            match_word(&line, "uint64_t")) {
            const char *q = line;
            const char *paren;
            char name[64];
            int ni = 0;

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
            while (*paren && *paren != '(') {
                paren++;
            }
            if (*paren != '(' || ni <= 0) {
                continue;
            }
            in_fn = 1;
            fn_is_kstart = (name[0] == 'k' && name[1] == 's' &&
                            name[2] == 't' && name[3] == 'a' &&
                            name[4] == 'r' && name[5] == 't');
            {
                char lbl[72];
                int i = 0;
                while (name[i] && i < 62) {
                    lbl[i] = name[i];
                    i++;
                }
                lbl[i++] = ':';
                lbl[i] = 0;
                asm_line(lbl);
            }
        }
        if (!in_fn) {
            continue;
        }
        skip_ws(&line);
        if (*line == '{') {
            continue;
        }
        if (*line == '}') {
            if (fn_is_kstart) {
                asm_line("mov rax, 0");
                asm_line("ret");
            }
            in_fn = 0;
            fn_is_kstart = 0;
            continue;
        }
        if (parse_outb_line(line)) {
            continue;
        }
        if (parse_return_int(line)) {
            continue;
        }
        if (match_word(&line, "return")) {
            asm_line("ret");
            continue;
        }
    }
    g_asm[g_asm_len] = 0;
    return chrisasm_assemble(g_asm, out);
}
