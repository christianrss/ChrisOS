/* LEARN:SH16-15 — subset C → asm → ChrisO */
#include "kcc.h"
#include "chrisasm.h"
#include <string.h>

#ifdef __freestanding__
#include "heap.h"
#include "fs.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

#define KCC_ASM_MAX (256u * 1024u)
#define KCC_PP_MAX (256u * 1024u)
#define KCC_SYM_MAX 256
#define KCC_MAC_MAX 96
#define KCC_STRUCT_MAX 16
#define KCC_FIELD_MAX 8
#define KCC_TYPEDEF_MAX 32

enum {
    TY_VOID = 0,
    TY_BOOL,
    TY_CHAR,
    TY_INT,
    TY_U8,
    TY_U16,
    TY_U32,
    TY_U64,
    TY_STRUCT
};

enum { LV_NONE = 0, LV_LOCAL = 1, LV_GLOBAL = 2, LV_ADDR = 3 };

typedef struct Type {
    int kind;
    int is_ptr;
    int pointee_size;
    int array_len;
    int struct_id;
    int size;
    int align;
} Type;

typedef struct Field {
    char name[32];
    Type type;
    int offset;
} Field;

typedef struct StructDef {
    char name[32];
    Field fields[KCC_FIELD_MAX];
    int nfield;
    int size;
    int align;
} StructDef;

typedef struct Sym {
    char name[64];
    char asm_name[64];
    Type type;
    int func;
    int is_static;
    int global;
    int frame;
    int alive;
} Sym;

typedef struct Macro {
    char name[64];
    char val[128];
    int used;
} Macro;

typedef struct Val {
    Type type;
    int lvalue;
    int func;
    int lv;
    int frame;
    char gname[64];
} Val;

static char g_asm[KCC_ASM_MAX];
static int g_asm_len;
static int g_asm_overflow;
static char g_pp[KCC_PP_MAX];
static int g_pp_len;
static KccDiag g_diag;
static const char *g_p;
static char g_file[96];
static int g_line;
static Macro g_mac[KCC_MAC_MAX];
static int g_nmac;
static StructDef g_struct[KCC_STRUCT_MAX];
static int g_nstruct;
static Sym g_sym[KCC_SYM_MAX];
static int g_nsym;
static char g_td_name[KCC_TYPEDEF_MAX][64];
static Type g_td_type[KCC_TYPEDEF_MAX];
static int g_ntd;
static int g_frame;
static int g_ntemp;
static int g_lab;

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
    g_diag.line = line < 1 ? 1 : line;
    g_diag.column = column < 1 ? 1 : column;
    g_diag.severity = 1;
    for (i = 0; msg[i] && i + 1u < sizeof(g_diag.message); i++) {
        g_diag.message[i] = msg[i];
    }
    return -1;
}

const KccDiag *kcc_last_error(void) {
    return &g_diag;
}

static int fail(const char *msg) {
    return diag_error(g_file, g_line, 1, msg);
}

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

static void asm_cat(const char *a, const char *b, const char *c, const char *d) {
    if (a) {
        asm_puts(a);
    }
    if (b) {
        asm_puts(b);
    }
    if (c) {
        asm_puts(c);
    }
    if (d) {
        asm_puts(d);
    }
    asm_puts("\n");
}

static void u64_dec(char *dst, uint64_t v) {
    char tmp[24];
    int n = 0;
    int i = 0;
    if (v == 0) {
        dst[0] = '0';
        dst[1] = 0;
        return;
    }
    while (v > 0 && n < 22) {
        tmp[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    while (n > 0) {
        dst[i++] = tmp[--n];
    }
    dst[i] = 0;
}

static void asm_mov_imm(const char *reg, uint64_t v) {
    char num[24];
    u64_dec(num, v);
    asm_cat("mov ", reg, ", ", num);
}

static void frame_txt(char *dst, int off) {
    char num[16];
    int n = off < 0 ? -off : off;
    int i = 0;
    dst[i++] = '[';
    dst[i++] = 'r';
    dst[i++] = 'b';
    dst[i++] = 'p';
    dst[i++] = off < 0 ? '-' : '+';
    u64_dec(num, (uint64_t)n);
    {
        int k = 0;
        while (num[k]) {
            dst[i++] = num[k++];
        }
    }
    dst[i++] = ']';
    dst[i] = 0;
}

static int is_ident_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= '0' && c <= '9');
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void copy_str(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap < 1) {
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int pp_putc(char c) {
    if (g_pp_len >= (int)KCC_PP_MAX - 1) {
        return -1;
    }
    g_pp[g_pp_len++] = c;
    return 0;
}

static int pp_puts(const char *s) {
    while (*s) {
        if (pp_putc(*s++) != 0) {
            return -1;
        }
    }
    return 0;
}

static int macro_find(const char *name) {
    int i;
    for (i = 0; i < g_nmac; i++) {
        if (g_mac[i].used && strcmp(g_mac[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int macro_add(const char *name, const char *val) {
    int id = macro_find(name);
    if (id < 0) {
        if (g_nmac >= KCC_MAC_MAX) {
            return -1;
        }
        id = g_nmac++;
    }
    g_mac[id].used = 1;
    copy_str(g_mac[id].name, 64, name);
    copy_str(g_mac[id].val, 128, val);
    return 0;
}

static int pp_expand_ident(const char *ident, int depth) {
    int id;
    const char *v;
    if (depth > 8) {
        return pp_puts(ident);
    }
    id = macro_find(ident);
    if (id < 0) {
        return pp_puts(ident);
    }
    v = g_mac[id].val;
    while (*v) {
        if (is_ident_start(*v)) {
            char name[64];
            int n = 0;
            while (is_ident_ch(*v) && n + 1 < 64) {
                name[n++] = *v++;
            }
            name[n] = 0;
            if (pp_expand_ident(name, depth + 1) != 0) {
                return -1;
            }
        } else {
            if (pp_putc(*v++) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int pp_expand_line(const char *line) {
    int i = 0;
    while (line[i]) {
        if (line[i] == '"') {
            if (pp_putc(line[i++]) != 0) {
                return -1;
            }
            while (line[i] && line[i] != '"') {
                if (line[i] == '\\' && line[i + 1]) {
                    if (pp_putc(line[i++]) != 0) {
                        return -1;
                    }
                }
                if (pp_putc(line[i++]) != 0) {
                    return -1;
                }
            }
            if (line[i] == '"') {
                if (pp_putc(line[i++]) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (is_ident_start(line[i])) {
            char name[64];
            int n = 0;
            while (is_ident_ch(line[i]) && n + 1 < 64) {
                name[n++] = line[i++];
            }
            name[n] = 0;
            if (pp_expand_ident(name, 0) != 0) {
                return -1;
            }
            continue;
        }
        if (pp_putc(line[i++]) != 0) {
            return -1;
        }
    }
    return pp_putc('\n');
}

static int pp_marker(const char *file, int line) {
    char num[16];
    u64_dec(num, (uint64_t)line);
    if (pp_puts("#line ") != 0 || pp_puts(num) != 0 || pp_puts(" \"") != 0 ||
        pp_puts(file) != 0 || pp_puts("\"\n") != 0) {
        return -1;
    }
    return 0;
}

static void dir_of(const char *file, char *dir, int cap) {
    int n = 0;
    int slash = -1;
    while (file[n]) {
        if (file[n] == '/' || file[n] == '\\') {
            slash = n;
        }
        n++;
    }
    if (slash < 0) {
        copy_str(dir, cap, ".");
        return;
    }
    if (slash >= cap) {
        slash = cap - 1;
    }
    memcpy(dir, file, (size_t)slash);
    dir[slash] = 0;
}

static int join_path(char *out, int cap, const char *dir, const char *name) {
    int n = 0;
    int i = 0;
    if (name[0] == '/' || name[0] == '\\') {
        copy_str(out, cap, name);
        return 0;
    }
    while (dir[i] && n + 1 < cap) {
        out[n++] = dir[i++];
    }
    if (n > 0 && out[n - 1] != '/' && n + 1 < cap) {
        out[n++] = '/';
    }
    i = 0;
    while (name[i] && n + 1 < cap) {
        out[n++] = name[i++];
    }
    out[n] = 0;
    return name[i] ? -1 : 0;
}

static int load_file(const char *path, char *buf, int cap) {
#ifdef __freestanding__
    int n = fs_read(path, buf, cap - 1);
    if (n < 0) {
        return -1;
    }
    buf[n] = 0;
    return n;
#else
    FILE *f = fopen(path, "rb");
    long sz;
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0 || sz >= cap) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return -1;
    }
    buf[sz] = 0;
    fclose(f);
    return (int)sz;
#endif
}

static int builtin_hdr(const char *name) {
    return strcmp(name, "stdint.h") == 0 || strcmp(name, "stdbool.h") == 0 ||
           strcmp(name, "stddef.h") == 0 || strcmp(name, "stdarg.h") == 0;
}

static int preprocess(const char *file, const char *src, int depth);

static int pp_include(const char *from, const char *name, int quoted, int depth) {
    char dir[128];
    char path[192];
    char *buf;
    int rc;
    if (!quoted && builtin_hdr(name)) {
        return 0;
    }
    dir_of(from, dir, (int)sizeof(dir));
    if (join_path(path, (int)sizeof(path), dir, name) != 0) {
        return -1;
    }
#ifdef __freestanding__
    buf = (char *)kmalloc(65536u);
#else
    buf = (char *)malloc(65536u);
#endif
    if (!buf) {
        return -1;
    }
    rc = load_file(path, buf, 65536);
    if (rc < 0) {
        if (join_path(path, (int)sizeof(path), "kernel/metal", name) == 0) {
            rc = load_file(path, buf, 65536);
        }
    }
    if (rc < 0) {
#ifdef __freestanding__
        kfree(buf);
#else
        free(buf);
#endif
        return -1;
    }
    rc = preprocess(path, buf, depth + 1);
#ifdef __freestanding__
    kfree(buf);
#else
    free(buf);
#endif
    return rc;
}

static void strip_comments(char *line, int *in_com) {
    char out[512];
    int i = 0;
    int n = 0;
    int in_str = 0;
    while (line[i] && n + 1 < (int)sizeof(out)) {
        if (*in_com) {
            if (line[i] == '*' && line[i + 1] == '/') {
                *in_com = 0;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (!in_str && line[i] == '/' && line[i + 1] == '/') {
            break;
        }
        if (!in_str && line[i] == '/' && line[i + 1] == '*') {
            *in_com = 1;
            i += 2;
            continue;
        }
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_str = !in_str;
        }
        out[n++] = line[i++];
    }
    out[n] = 0;
    memcpy(line, out, (size_t)n + 1u);
}

static int handle_directive(const char *file, const char *dir, int *skip, int *sp,
                            int *stack, int depth, int *include_err) {
    const char *p = dir;
    char name[64];
    int n = 0;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '#') {
        return 1;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (is_ident_ch(*p) && n + 1 < 64) {
        name[n++] = *p++;
    }
    name[n] = 0;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (strcmp(name, "ifndef") == 0 || strcmp(name, "ifdef") == 0) {
        char mname[64];
        int defined;
        int k = 0;
        int parent = *skip;
        while (is_ident_ch(*p) && k + 1 < 64) {
            mname[k++] = *p++;
        }
        mname[k] = 0;
        defined = macro_find(mname) >= 0;
        if (*sp >= 32) {
            return -1;
        }
        stack[*sp] = parent;
        (*sp)++;
        if (parent) {
            *skip = 1;
        } else if (strcmp(name, "ifndef") == 0) {
            *skip = defined;
        } else {
            *skip = !defined;
        }
        return 0;
    }
    if (strcmp(name, "else") == 0) {
        int parent = *sp > 0 ? stack[*sp - 1] : 0;
        if (parent) {
            *skip = 1;
        } else {
            *skip = !*skip;
        }
        return 0;
    }
    if (strcmp(name, "endif") == 0) {
        if (*sp <= 0) {
            return -1;
        }
        (*sp)--;
        *skip = stack[*sp];
        return 0;
    }
    if (*skip) {
        return 0;
    }
    if (strcmp(name, "define") == 0) {
        char mname[64];
        char val[128];
        int k = 0;
        int vi = 0;
        while (is_ident_ch(*p) && k + 1 < 64) {
            mname[k++] = *p++;
        }
        mname[k] = 0;
        if (*p == '(') {
            return 0;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p && *p != '\r' && vi + 1 < 128) {
            val[vi++] = *p++;
        }
        while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t')) {
            vi--;
        }
        val[vi] = 0;
        if (macro_add(mname, val) != 0) {
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "include") == 0) {
        char iname[96];
        int k = 0;
        int quoted = 0;
        char end;
        if (*p == '"') {
            quoted = 1;
            end = '"';
            p++;
        } else if (*p == '<') {
            end = '>';
            p++;
        } else {
            *include_err = 1;
            return -1;
        }
        while (*p && *p != end && k + 1 < 96) {
            iname[k++] = *p++;
        }
        iname[k] = 0;
        if (pp_include(file, iname, quoted, depth) != 0) {
            *include_err = 1;
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "pragma") == 0 || strcmp(name, "error") == 0 ||
        strcmp(name, "warning") == 0) {
        return 0;
    }
    return -1;
}

static int preprocess(const char *file, const char *src, int depth) {
    int line_no = 1;
    int in_com = 0;
    int skip = 0;
    int sp = 0;
    int stack[32];
    const char *p = src;
    if (depth > 16) {
        return -1;
    }
    while (*p) {
        char line[512];
        int n = 0;
        int include_err = 0;
        int rc;
        const char *probe;
        while (*p && *p != '\n' && n + 1 < (int)sizeof(line)) {
            line[n++] = *p++;
        }
        line[n] = 0;
        if (*p == '\n') {
            p++;
        }
        strip_comments(line, &in_com);
        probe = line;
        while (*probe == ' ' || *probe == '\t') {
            probe++;
        }
        if (*probe == '#') {
            rc = handle_directive(file, probe, &skip, &sp, stack, depth, &include_err);
            if (rc < 0) {
                if (include_err) {
                    copy_str(g_file, 96, file);
                    g_line = line_no;
                    return fail("cannot read include");
                }
                copy_str(g_file, 96, file);
                g_line = line_no;
                return fail("preprocessor directive");
            }
            line_no++;
            continue;
        }
        if (!skip && !in_com && probe[0] != 0) {
            if (pp_marker(file, line_no) != 0 || pp_expand_line(line) != 0) {
                return fail("preprocessor buffer is full");
            }
        }
        line_no++;
    }
    return 0;
}

static int parse_hash(void) {
    char name[32];
    int n = 0;
    if (*g_p != '#') {
        return 0;
    }
    g_p++;
    while (*g_p == ' ' || *g_p == '\t') {
        g_p++;
    }
    while (is_ident_ch(*g_p) && n + 1 < 32) {
        name[n++] = *g_p++;
    }
    name[n] = 0;
    if (strcmp(name, "line") != 0) {
        return -1;
    }
    while (*g_p == ' ' || *g_p == '\t') {
        g_p++;
    }
    g_line = 0;
    while (*g_p >= '0' && *g_p <= '9') {
        g_line = g_line * 10 + (*g_p - '0');
        g_p++;
    }
    while (*g_p == ' ' || *g_p == '\t') {
        g_p++;
    }
    if (*g_p == '"') {
        int i = 0;
        g_p++;
        while (*g_p && *g_p != '"' && i + 1 < (int)sizeof(g_file)) {
            g_file[i++] = *g_p++;
        }
        g_file[i] = 0;
        if (*g_p == '"') {
            g_p++;
        }
    }
    while (*g_p && *g_p != '\n') {
        g_p++;
    }
    if (*g_p == '\n') {
        g_p++;
    }
    return 1;
}

static void skip(void) {
    for (;;) {
        while (*g_p == ' ' || *g_p == '\t' || *g_p == '\r') {
            g_p++;
        }
        if (*g_p == '\n') {
            g_p++;
            g_line++;
            continue;
        }
        if (*g_p == '#') {
            if (parse_hash() < 0) {
                return;
            }
            continue;
        }
        return;
    }
}

static int eat_op(const char *s) {
    int n = 0;
    skip();
    while (s[n]) {
        if (g_p[n] != s[n]) {
            return 0;
        }
        n++;
    }
    g_p += n;
    return 1;
}

static int eat_kw(const char *w) {
    int n = 0;
    skip();
    while (w[n]) {
        if (g_p[n] != w[n]) {
            return 0;
        }
        n++;
    }
    if (is_ident_ch(g_p[n])) {
        return 0;
    }
    g_p += n;
    return 1;
}

static int take_ident(char *out, int cap) {
    int n = 0;
    skip();
    if (!is_ident_start(*g_p)) {
        return 0;
    }
    while (is_ident_ch(*g_p)) {
        if (n + 1 < cap) {
            out[n++] = *g_p;
        }
        g_p++;
    }
    out[n] = 0;
    return 1;
}

static int take_number(uint64_t *out) {
    uint64_t v = 0;
    int hex = 0;
    skip();
    if (*g_p < '0' || *g_p > '9') {
        return 0;
    }
    if (g_p[0] == '0' && (g_p[1] == 'x' || g_p[1] == 'X')) {
        hex = 1;
        g_p += 2;
    }
    if (hex) {
        if (!((*g_p >= '0' && *g_p <= '9') || (*g_p >= 'a' && *g_p <= 'f') ||
              (*g_p >= 'A' && *g_p <= 'F'))) {
            return 0;
        }
        while ((*g_p >= '0' && *g_p <= '9') || (*g_p >= 'a' && *g_p <= 'f') ||
               (*g_p >= 'A' && *g_p <= 'F')) {
            int d;
            if (*g_p >= '0' && *g_p <= '9') {
                d = *g_p - '0';
            } else if (*g_p >= 'a' && *g_p <= 'f') {
                d = 10 + *g_p - 'a';
            } else {
                d = 10 + *g_p - 'A';
            }
            v = (v << 4) + (uint64_t)d;
            g_p++;
        }
    } else {
        while (*g_p >= '0' && *g_p <= '9') {
            v = v * 10ull + (uint64_t)(*g_p - '0');
            g_p++;
        }
    }
    while (*g_p == 'u' || *g_p == 'U' || *g_p == 'l' || *g_p == 'L') {
        g_p++;
    }
    *out = v;
    return 1;
}

static int take_char(uint64_t *out) {
    char c;
    skip();
    if (*g_p != '\'') {
        return 0;
    }
    g_p++;
    c = *g_p++;
    if (c == '\\') {
        c = *g_p++;
        if (c == 'n') {
            c = '\n';
        } else if (c == 'r') {
            c = '\r';
        } else if (c == '0') {
            c = 0;
        }
    }
    if (*g_p == '\'') {
        g_p++;
    }
    *out = (unsigned char)c;
    return 1;
}

static int take_string(char *out, int cap, int *nlen) {
    int n = 0;
    skip();
    if (*g_p != '"') {
        return 0;
    }
    g_p++;
    while (*g_p && *g_p != '"') {
        char c = *g_p++;
        if (c == '\\' && *g_p) {
            c = *g_p++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == '0') {
                c = 0;
            }
        }
        if (n + 1 < cap) {
            out[n] = c;
        }
        n++;
    }
    if (*g_p == '"') {
        g_p++;
    }
    if (n < cap) {
        out[n] = 0;
    }
    *nlen = n;
    return 1;
}

static Type type_make(int kind, int size, int align) {
    Type t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    t.size = size;
    t.align = align;
    t.array_len = -1;
    t.struct_id = -1;
    t.pointee_size = size;
    return t;
}

static Type type_ptr(Type elem) {
    Type t = type_make(elem.kind, 8, 8);
    t.is_ptr = 1;
    t.pointee_size = elem.array_len >= 0 ? elem.pointee_size : elem.size;
    if (t.pointee_size < 1) {
        t.pointee_size = 1;
    }
    t.struct_id = elem.struct_id;
    return t;
}

static int td_find(const char *name) {
    int i;
    for (i = 0; i < g_ntd; i++) {
        if (strcmp(g_td_name[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int sym_find(const char *name) {
    int i;
    for (i = g_nsym - 1; i >= 0; i--) {
        if (g_sym[i].alive && strcmp(g_sym[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int sym_add(const Sym *s) {
    int id;
    if (g_nsym >= KCC_SYM_MAX) {
        return -1;
    }
    id = g_nsym++;
    g_sym[id] = *s;
    g_sym[id].alive = 1;
    return id;
}

static int is_type_name(const char *name) {
    if (strcmp(name, "void") == 0 || strcmp(name, "bool") == 0 ||
        strcmp(name, "char") == 0 || strcmp(name, "int") == 0 ||
        strcmp(name, "uint8_t") == 0 || strcmp(name, "uint16_t") == 0 ||
        strcmp(name, "uint32_t") == 0 || strcmp(name, "uint64_t") == 0) {
        return 1;
    }
    return td_find(name) >= 0;
}

static int peek_type_start(void) {
    const char *save = g_p;
    int line = g_line;
    char name[64];
    int yes = 0;
    skip();
    if (eat_kw("static") || eat_kw("const") || eat_kw("volatile") ||
        eat_kw("unsigned") || eat_kw("inline") || eat_kw("struct")) {
        yes = 1;
    } else if (take_ident(name, 64) && is_type_name(name)) {
        yes = 1;
    }
    g_p = save;
    g_line = line;
    return yes;
}

static int type_from_name(const char *name, Type *t) {
    int id;
    if (strcmp(name, "void") == 0) {
        *t = type_make(TY_VOID, 0, 1);
        return 1;
    }
    if (strcmp(name, "bool") == 0) {
        *t = type_make(TY_BOOL, 1, 1);
        return 1;
    }
    if (strcmp(name, "char") == 0) {
        *t = type_make(TY_CHAR, 1, 1);
        return 1;
    }
    if (strcmp(name, "int") == 0) {
        *t = type_make(TY_INT, 8, 8);
        return 1;
    }
    if (strcmp(name, "uint8_t") == 0) {
        *t = type_make(TY_U8, 1, 1);
        return 1;
    }
    if (strcmp(name, "uint16_t") == 0) {
        *t = type_make(TY_U16, 2, 2);
        return 1;
    }
    if (strcmp(name, "uint32_t") == 0) {
        *t = type_make(TY_U32, 4, 4);
        return 1;
    }
    if (strcmp(name, "uint64_t") == 0) {
        *t = type_make(TY_U64, 8, 8);
        return 1;
    }
    id = td_find(name);
    if (id >= 0) {
        *t = g_td_type[id];
        return 1;
    }
    return 0;
}

static int parse_expr(Val *out);
static int parse_stmt(void);

static int parse_struct_body(int id) {
    int off = 0;
    int max_align = 1;
    g_struct[id].nfield = 0;
    if (!eat_op("{")) {
        return fail("expected struct body");
    }
    while (!eat_op("}")) {
        Type ft;
        char fname[32];
        int is_static = 0;
        int is_inline = 0;
        int is_unsigned = 0;
        char tname[64];
        Field *f;
        int al;
        if (g_struct[id].nfield >= KCC_FIELD_MAX) {
            return fail("too many fields");
        }
        for (;;) {
            if (eat_kw("const") || eat_kw("volatile")) {
                continue;
            }
            if (eat_kw("unsigned")) {
                is_unsigned = 1;
                continue;
            }
            break;
        }
        if (!take_ident(tname, 64) || !type_from_name(tname, &ft)) {
            if (is_unsigned) {
                ft = type_make(TY_U32, 4, 4);
            } else {
                return fail("field type expected");
            }
        } else if (is_unsigned && ft.kind == TY_INT) {
            ft = type_make(TY_U32, 4, 4);
        }
        (void)is_static;
        (void)is_inline;
        while (eat_op("*")) {
            ft = type_ptr(ft);
        }
        if (!take_ident(fname, 32)) {
            return fail("field name expected");
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        al = ft.align < 1 ? 1 : ft.align;
        off = (off + al - 1) & ~(al - 1);
        f = &g_struct[id].fields[g_struct[id].nfield++];
        memset(f, 0, sizeof(*f));
        copy_str(f->name, 32, fname);
        f->type = ft;
        f->offset = off;
        off += ft.size < 1 ? 1 : ft.size;
        if (al > max_align) {
            max_align = al;
        }
    }
    g_struct[id].align = max_align;
    g_struct[id].size = (off + max_align - 1) & ~(max_align - 1);
    if (g_struct[id].size < 1) {
        g_struct[id].size = 1;
    }
    return 0;
}

static int parse_base(Type *t, int *is_static, int *is_inline) {
    int is_unsigned = 0;
    char name[64];
    *is_static = 0;
    *is_inline = 0;
    for (;;) {
        if (eat_kw("static")) {
            *is_static = 1;
            continue;
        }
        if (eat_kw("inline")) {
            *is_inline = 1;
            continue;
        }
        if (eat_kw("const") || eat_kw("volatile")) {
            continue;
        }
        if (eat_kw("unsigned")) {
            is_unsigned = 1;
            continue;
        }
        break;
    }
    if (eat_kw("struct")) {
        int id;
        if (g_nstruct >= KCC_STRUCT_MAX) {
            return fail("too many structs");
        }
        id = g_nstruct++;
        memset(&g_struct[id], 0, sizeof(g_struct[id]));
        if (parse_struct_body(id) != 0) {
            return -1;
        }
        *t = type_make(TY_STRUCT, g_struct[id].size, g_struct[id].align);
        t->struct_id = id;
        while (eat_op("*")) {
            *t = type_ptr(*t);
        }
        return 0;
    }
    if (!take_ident(name, 64)) {
        if (is_unsigned) {
            *t = type_make(TY_U32, 4, 4);
            while (eat_op("*")) {
                *t = type_ptr(*t);
            }
            return 0;
        }
        return fail("type expected");
    }
    if (!type_from_name(name, t)) {
        return fail("type expected");
    }
    if (is_unsigned && t->kind == TY_INT) {
        *t = type_make(TY_U32, 4, 4);
    }
    while (eat_op("*")) {
        *t = type_ptr(*t);
    }
    return 0;
}

static int new_lab(char *buf) {
    char num[16];
    int i = 0;
    u64_dec(num, (uint64_t)g_lab++);
    buf[i++] = '.';
    buf[i++] = 'L';
    {
        int k = 0;
        while (num[k]) {
            buf[i++] = num[k++];
        }
    }
    buf[i] = 0;
    return 0;
}

static void asm_label(const char *name) {
    asm_cat(name, ":", 0, 0);
}

static int alloc_slot(int size) {
    int s = size < 8 ? 8 : size;
    s = (s + 7) & ~7;
    if (g_frame + s > 300) {
        return 0;
    }
    g_frame += s;
    return -g_frame;
}

static int temp_slot(void) {
    int off = -320 - g_ntemp * 8;
    g_ntemp++;
    return off;
}

static void store_rax_frame(int off) {
    char mem[32];
    frame_txt(mem, off);
    asm_cat("mov ", mem, ", rax", 0);
}

static void load_frame_rax(int off) {
    char mem[32];
    frame_txt(mem, off);
    asm_cat("mov rax, ", mem, 0, 0);
}

static int save_rax(void) {
    int off = temp_slot();
    store_rax_frame(off);
    return off;
}

static void gen_addr(const Val *v) {
    char mem[32];
    if (v->lv == LV_LOCAL) {
        frame_txt(mem, v->frame);
        asm_cat("lea rax, ", mem, 0, 0);
        return;
    }
    if (v->lv == LV_GLOBAL) {
        asm_cat("lea rax, [rel ", v->gname, "]", 0);
        return;
    }
    load_frame_rax(v->frame);
}

static void load_val(Val *v) {
    if (v->func) {
        return;
    }
    if (v->type.array_len >= 0 && v->lvalue) {
        gen_addr(v);
        v->lvalue = 0;
        v->type = type_ptr(v->type);
        v->lv = LV_NONE;
        return;
    }
    if (!v->lvalue) {
        return;
    }
    if (v->lv == LV_LOCAL) {
        load_frame_rax(v->frame);
    } else if (v->lv == LV_GLOBAL) {
        if (v->type.size <= 1 && !v->type.is_ptr) {
            asm_cat("movzx rax, byte [rel ", v->gname, "]", 0);
        } else {
            asm_cat("mov rax, [rel ", v->gname, "]", 0);
        }
    } else if (v->lv == LV_ADDR) {
        gen_addr(v);
        if (v->type.size <= 1 && !v->type.is_ptr) {
            asm_line("movzx rax, byte [rax]");
        } else {
            asm_line("mov rcx, rax");
            asm_line("mov rax, [rcx]");
        }
    }
    v->lvalue = 0;
    v->lv = LV_NONE;
}

static void store_val(const Val *dst) {
    int tmp;
    char mem[32];
    if (dst->lv == LV_LOCAL) {
        store_rax_frame(dst->frame);
        return;
    }
    if (dst->lv == LV_GLOBAL && dst->type.size <= 1 && !dst->type.is_ptr) {
        asm_cat("mov byte [rel ", dst->gname, "], al", 0);
        return;
    }
    if (dst->lv == LV_GLOBAL) {
        asm_cat("mov [rel ", dst->gname, "], rax", 0);
        return;
    }
    tmp = save_rax();
    gen_addr(dst);
    asm_line("mov rcx, rax");
    load_frame_rax(tmp);
    if (dst->type.size <= 1 && !dst->type.is_ptr) {
        asm_line("mov byte [rcx], al");
    } else {
        asm_line("mov [rcx], rax");
    }
    (void)mem;
}

static void set_flag(const char *jcc) {
    char yes[16];
    char done[16];
    new_lab(yes);
    new_lab(done);
    asm_cat(jcc, " ", yes, 0);
    asm_mov_imm("rax", 0);
    asm_cat("jmp ", done, 0, 0);
    asm_label(yes);
    asm_mov_imm("rax", 1);
    asm_label(done);
}

static int emit_ro_string(const char *s, int n, char *lab) {
    int i;
    new_lab(lab);
    asm_line(".rodata");
    asm_label(lab);
    asm_puts(".asciz \"");
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' || c == '"') {
            asm_puts("\\");
            asm_puts(c == '"' ? "\"" : "\\");
        } else if (c == '\n') {
            asm_puts("\\n");
        } else {
            char b[2];
            b[0] = c;
            b[1] = 0;
            asm_puts(b);
        }
    }
    asm_line("\"");
    asm_line(".text");
    return 0;
}

static int parse_unary(Val *out);

static int parse_primary(Val *out) {
    uint64_t num;
    char ident[64];
    char str[128];
    int slen = 0;
    memset(out, 0, sizeof(*out));
    out->type.array_len = -1;
    if (eat_kw("true")) {
        asm_mov_imm("rax", 1);
        out->type = type_make(TY_BOOL, 1, 1);
        return 0;
    }
    if (eat_kw("false")) {
        asm_mov_imm("rax", 0);
        out->type = type_make(TY_BOOL, 1, 1);
        return 0;
    }
    if (take_number(&num)) {
        asm_mov_imm("rax", num);
        out->type = type_make(TY_U64, 8, 8);
        return 0;
    }
    if (take_char(&num)) {
        asm_mov_imm("rax", num);
        out->type = type_make(TY_CHAR, 1, 1);
        return 0;
    }
    if (take_string(str, 128, &slen)) {
        char lab[16];
        emit_ro_string(str, slen, lab);
        asm_cat("lea rax, [rel ", lab, "]", 0);
        out->type = type_ptr(type_make(TY_CHAR, 1, 1));
        return 0;
    }
    if (eat_op("(")) {
        if (parse_expr(out) != 0) {
            return -1;
        }
        if (!eat_op(")")) {
            return fail("expected closing parenthesis");
        }
        return 0;
    }
    if (!take_ident(ident, 64)) {
        return fail("expression expected");
    }
    {
        int id = sym_find(ident);
        if (id < 0) {
            skip();
            if (*g_p == '(') {
                out->func = 1;
                copy_str(out->gname, 64, ident);
                out->type = type_make(TY_INT, 8, 8);
                return 0;
            }
            return fail("unknown name");
        }
        out->type = g_sym[id].type;
        if (g_sym[id].func) {
            out->func = 1;
            copy_str(out->gname, 64, g_sym[id].asm_name);
            return 0;
        }
        out->lvalue = 1;
        if (g_sym[id].global) {
            out->lv = LV_GLOBAL;
            copy_str(out->gname, 64, g_sym[id].asm_name);
        } else {
            out->lv = LV_LOCAL;
            out->frame = g_sym[id].frame;
        }
        return 0;
    }
}

static int parse_args(const char *name) {
    int slots[6];
    int n = 0;
    static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int i;
    if (!eat_op(")")) {
        do {
            Val arg;
            if (n >= 6) {
                return fail("too many arguments");
            }
            if (parse_expr(&arg) != 0) {
                return -1;
            }
            load_val(&arg);
            slots[n++] = save_rax();
        } while (eat_op(","));
        if (!eat_op(")")) {
            return fail("expected closing parenthesis");
        }
    }
    for (i = 0; i < n; i++) {
        load_frame_rax(slots[i]);
        asm_cat("mov ", regs[i], ", rax", 0);
    }
    asm_cat("call ", name, 0, 0);
    return 0;
}

static int inc_lv(Val *v, int delta, int prefix) {
    int step = 1;
    int old;
    if (!v->lvalue) {
        return fail("operand is not assignable");
    }
    Val dest;
    if (v->type.is_ptr) {
        step = v->type.pointee_size < 1 ? 1 : v->type.pointee_size;
    }
    dest = *v;
    load_val(v);
    if (!prefix) {
        old = save_rax();
    } else {
        old = 0;
    }
    if (delta < 0) {
        asm_mov_imm("rcx", (uint64_t)step);
        asm_line("sub rax, rcx");
    } else {
        asm_mov_imm("rcx", (uint64_t)step);
        asm_line("add rax, rcx");
    }
    store_val(&dest);
    if (!prefix) {
        load_frame_rax(old);
    }
    v->lvalue = 0;
    v->lv = LV_NONE;
    return 0;
}

static int parse_postfix(Val *out) {
    if (parse_primary(out) != 0) {
        return -1;
    }
    for (;;) {
        if (eat_op("(")) {
            if (!out->func) {
                return fail("call target is not a function");
            }
            if (parse_args(out->gname) != 0) {
                return -1;
            }
            out->func = 0;
            out->lvalue = 0;
            out->lv = LV_NONE;
            if (out->type.size < 1) {
                out->type = type_make(TY_INT, 8, 8);
            }
            continue;
        }
        if (eat_op("[")) {
            Val idx;
            int base;
            int elem;
            Type elem_ty;
            if (out->type.array_len >= 0) {
                elem = out->type.pointee_size < 1 ? 1 : out->type.pointee_size;
                elem_ty = out->type;
                elem_ty.array_len = -1;
                elem_ty.size = elem;
                elem_ty.is_ptr = 0;
            } else if (out->type.is_ptr) {
                elem = out->type.pointee_size < 1 ? 1 : out->type.pointee_size;
                elem_ty = type_make(out->type.kind, elem, elem < 8 ? elem : 8);
            } else {
                return fail("indexed value is not an array");
            }
            load_val(out);
            base = save_rax();
            if (parse_expr(&idx) != 0) {
                return -1;
            }
            if (!eat_op("]")) {
                return fail("expected closing bracket");
            }
            load_val(&idx);
            if (elem != 1) {
                asm_mov_imm("rcx", (uint64_t)elem);
                asm_line("imul rax, rcx");
            }
            asm_line("mov rcx, rax");
            load_frame_rax(base);
            asm_line("add rax, rcx");
            out->lvalue = 1;
            out->func = 0;
            out->lv = LV_ADDR;
            out->frame = save_rax();
            out->type = elem_ty;
            continue;
        }
        if (eat_op("++")) {
            if (inc_lv(out, 1, 0) != 0) {
                return -1;
            }
            continue;
        }
        if (eat_op("--")) {
            if (inc_lv(out, -1, 0) != 0) {
                return -1;
            }
            continue;
        }
        break;
    }
    return 0;
}

static int parse_unary(Val *out) {
    if (eat_op("++")) {
        if (parse_unary(out) != 0) {
            return -1;
        }
        return inc_lv(out, 1, 1);
    }
    if (eat_op("--")) {
        if (parse_unary(out) != 0) {
            return -1;
        }
        return inc_lv(out, -1, 1);
    }
    if (eat_op("!")) {
        if (parse_unary(out) != 0) {
            return -1;
        }
        load_val(out);
        asm_line("cmp rax, 0");
        set_flag("je");
        out->lvalue = 0;
        out->type = type_make(TY_BOOL, 1, 1);
        return 0;
    }
    if (eat_op("&")) {
        if (parse_unary(out) != 0) {
            return -1;
        }
        if (!out->lvalue) {
            return fail("operand is not addressable");
        }
        gen_addr(out);
        out->type = type_ptr(out->type);
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    if (eat_op("*")) {
        if (parse_unary(out) != 0) {
            return -1;
        }
        load_val(out);
        if (!out->type.is_ptr && out->type.array_len < 0) {
            return fail("dereference of a non-pointer");
        }
        {
            int elem = out->type.pointee_size < 1 ? 1 : out->type.pointee_size;
            Type et = type_make(out->type.kind, elem, elem < 8 ? elem : 8);
            out->frame = save_rax();
            out->lv = LV_ADDR;
            out->lvalue = 1;
            out->func = 0;
            out->type = et;
        }
        return 0;
    }
    if (eat_op("(")) {
        if (peek_type_start()) {
            Type t;
            int is_static = 0;
            int is_inline = 0;
            if (parse_base(&t, &is_static, &is_inline) != 0) {
                return -1;
            }
            if (!eat_op(")")) {
                return fail("expected closing parenthesis");
            }
            if (parse_unary(out) != 0) {
                return -1;
            }
            load_val(out);
            if (t.size == 1) {
                asm_line("and rax, 255");
            }
            out->type = t;
            out->lvalue = 0;
            return 0;
        }
        if (parse_expr(out) != 0) {
            return -1;
        }
        if (!eat_op(")")) {
            return fail("expected closing parenthesis");
        }
        return 0;
    }
    return parse_postfix(out);
}

static int parse_binary(Val *out, int prec);

static int apply_bin(Val *left, Val *right, const char *kind) {
    int ls;
    load_val(left);
    ls = save_rax();
    load_val(right);
    if (strcmp(kind, "shr") == 0 || strcmp(kind, "shl") == 0) {
        asm_line("mov rcx, rax");
        load_frame_rax(ls);
        asm_cat(kind, " rax, cl", 0, 0);
        left->lvalue = 0;
        left->lv = LV_NONE;
        return 0;
    }
    if (strcmp(kind, "div") == 0 || strcmp(kind, "mod") == 0) {
        asm_line("mov rcx, rax");
        load_frame_rax(ls);
        asm_line("xor rdx, rdx");
        asm_line("div rcx");
        if (strcmp(kind, "mod") == 0) {
            asm_line("mov rax, rdx");
        }
        left->lvalue = 0;
        left->lv = LV_NONE;
        return 0;
    }
    asm_line("mov rcx, rax");
    load_frame_rax(ls);
    if (strcmp(kind, "add") == 0) {
        asm_line("add rax, rcx");
    } else if (strcmp(kind, "sub") == 0) {
        asm_line("sub rax, rcx");
    } else if (strcmp(kind, "and") == 0) {
        asm_line("and rax, rcx");
    } else if (strcmp(kind, "or") == 0) {
        asm_line("or rax, rcx");
    } else if (strcmp(kind, "xor") == 0) {
        asm_line("xor rax, rcx");
    } else if (strcmp(kind, "mul") == 0) {
        asm_line("imul rax, rcx");
    } else {
        asm_line("cmp rax, rcx");
        set_flag(kind);
        left->type = type_make(TY_BOOL, 1, 1);
    }
    left->lvalue = 0;
    left->lv = LV_NONE;
    return 0;
}

static int parse_expr(Val *out) {
    return parse_binary(out, 0);
}

static int parse_binary(Val *out, int prec) {
    static const char *ops[] = {"||", "&&", "|",  "^",  "&",  "==", "!=", "<",
                                ">",  "<=", ">=", "<<", ">>", "+",  "-",  "*",
                                "/",  "%"};
    static const int prec_of[] = {1, 2, 3, 4, 5, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10};
    static const char *kind[] = {"lor", "land", "or",  "xor", "and", "je", "jne", "jl",
                                 "jg",  "jle",  "jge", "shl", "shr", "add", "sub", "mul",
                                 "div", "mod"};
    int nops = 18;
    if (parse_unary(out) != 0) {
        return -1;
    }
    for (;;) {
        int i;
        int matched = -1;
        skip();
        for (i = 0; i < nops; i++) {
            int n = (int)strlen(ops[i]);
            if (prec_of[i] < prec) {
                continue;
            }
            if (strncmp(g_p, ops[i], (size_t)n) == 0) {
                    if (n == 1 && (g_p[1] == '=' || g_p[1] == ops[i][0])) {
                        continue;
                    }
                    if (matched >= 0 && (int)strlen(ops[matched]) >= n) {
                        continue;
                    }
                    matched = i;
                }
            }
            if (matched < 0) {
                break;
            }
            g_p += strlen(ops[matched]);
            if (strcmp(kind[matched], "land") == 0 || strcmp(kind[matched], "lor") == 0) {
                char lab[16];
                char done[16];
                load_val(out);
                new_lab(lab);
                new_lab(done);
                asm_line("cmp rax, 0");
                if (strcmp(kind[matched], "land") == 0) {
                    asm_cat("je ", lab, 0, 0);
                } else {
                    asm_cat("jne ", lab, 0, 0);
                }
                {
                    Val right;
                    if (parse_binary(&right, prec_of[matched] + 1) != 0) {
                        return -1;
                    }
                    load_val(&right);
                    asm_line("cmp rax, 0");
                    set_flag("jne");
                    asm_cat("jmp ", done, 0, 0);
                }
                asm_label(lab);
                asm_mov_imm("rax", strcmp(kind[matched], "land") == 0 ? 0 : 1);
                asm_label(done);
                out->lvalue = 0;
                out->type = type_make(TY_BOOL, 1, 1);
                continue;
            }
            {
                Val right;
                if (parse_binary(&right, prec_of[matched] + 1) != 0) {
                    return -1;
                }
                if (apply_bin(out, &right, kind[matched]) != 0) {
                    return -1;
                }
            }
        }
    if (prec != 0) {
        return 0;
    }
    if (eat_op("-=")) {
        Val right;
        Val saved;
        int ls;
        if (!out->lvalue) {
            return fail("operand is not assignable");
        }
        saved = *out;
        load_val(out);
        ls = save_rax();
        if (parse_expr(&right) != 0) {
            return -1;
        }
        load_val(&right);
        asm_line("mov rcx, rax");
        load_frame_rax(ls);
        asm_line("sub rax, rcx");
        store_val(&saved);
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    if (eat_op("+=")) {
        Val right;
        Val saved;
        int ls;
        if (!out->lvalue) {
            return fail("operand is not assignable");
        }
        saved = *out;
        load_val(out);
        ls = save_rax();
        if (parse_expr(&right) != 0) {
            return -1;
        }
        load_val(&right);
        asm_line("mov rcx, rax");
        load_frame_rax(ls);
        asm_line("add rax, rcx");
        store_val(&saved);
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    if (eat_op("/=")) {
        Val right;
        Val saved;
        int ls;
        if (!out->lvalue) {
            return fail("operand is not assignable");
        }
        saved = *out;
        load_val(out);
        ls = save_rax();
        if (parse_expr(&right) != 0) {
            return -1;
        }
        load_val(&right);
        asm_line("mov rcx, rax");
        load_frame_rax(ls);
        asm_line("xor rdx, rdx");
        asm_line("div rcx");
        store_val(&saved);
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    skip();
    if (g_p[0] == '=' && g_p[1] != '=') {
        Val right;
        Val saved;
        if (!out->lvalue || out->type.array_len >= 0) {
            return fail("operand is not assignable");
        }
        g_p++;
        saved = *out;
        if (parse_expr(&right) != 0) {
            return -1;
        }
        load_val(&right);
        store_val(&saved);
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    return 0;
}

static int grab_until(char *dst, int cap, char stop) {
    int depth = 0;
    int n = 0;
    skip();
    while (*g_p) {
        if (depth == 0 && *g_p == stop) {
            break;
        }
        if (*g_p == '(' || *g_p == '[') {
            depth++;
        } else if ((*g_p == ')' || *g_p == ']') && depth > 0) {
            depth--;
        }
        if (n + 1 < cap) {
            dst[n++] = *g_p;
        }
        if (*g_p == '\n') {
            g_line++;
        }
        g_p++;
    }
    dst[n] = 0;
    return *g_p == stop ? 0 : -1;
}

static int parse_from(const char *text, Val *out) {
    const char *save = g_p;
    int line = g_line;
    int rc;
    g_p = text;
    rc = parse_expr(out);
    g_p = save;
    g_line = line;
    return rc;
}

static void epilogue(void) {
    asm_line("mov rsp, rbp");
    asm_line("pop rbp");
    asm_line("ret");
}

static int parse_stmt(void) {
    g_ntemp = 0;
    skip();
    if (*g_p == 0) {
        return fail("unexpected end of function");
    }
    if (eat_op("{")) {
        while (!eat_op("}")) {
            if (parse_stmt() != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (eat_kw("if")) {
        Val cond;
        char else_lab[16];
        char end_lab[16];
        new_lab(else_lab);
        new_lab(end_lab);
        if (!eat_op("(") || parse_expr(&cond) != 0 || !eat_op(")")) {
            return fail("bad if");
        }
        load_val(&cond);
        asm_line("cmp rax, 0");
        asm_cat("je ", else_lab, 0, 0);
        if (parse_stmt() != 0) {
            return -1;
        }
        if (eat_kw("else")) {
            asm_cat("jmp ", end_lab, 0, 0);
            asm_label(else_lab);
            if (parse_stmt() != 0) {
                return -1;
            }
            asm_label(end_lab);
        } else {
            asm_label(else_lab);
        }
        return 0;
    }
    if (eat_kw("while")) {
        Val cond;
        char head[16];
        char end_lab[16];
        new_lab(head);
        new_lab(end_lab);
        if (!eat_op("(")) {
            return fail("bad while");
        }
        asm_label(head);
        if (parse_expr(&cond) != 0 || !eat_op(")")) {
            return fail("bad while");
        }
        load_val(&cond);
        asm_line("cmp rax, 0");
        asm_cat("je ", end_lab, 0, 0);
        if (parse_stmt() != 0) {
            return -1;
        }
        asm_cat("jmp ", head, 0, 0);
        asm_label(end_lab);
        return 0;
    }
    if (eat_kw("for")) {
        char initb[192];
        char condb[192];
        char stepb[192];
        char head[16];
        char end_lab[16];
        Val discard;
        new_lab(head);
        new_lab(end_lab);
        if (!eat_op("(")) {
            return fail("bad for");
        }
        if (grab_until(initb, 192, ';') != 0 || !eat_op(";")) {
            return fail("bad for");
        }
        if (grab_until(condb, 192, ';') != 0 || !eat_op(";")) {
            return fail("bad for");
        }
        if (grab_until(stepb, 192, ')') != 0 || !eat_op(")")) {
            return fail("bad for");
        }
        if (initb[0] && parse_from(initb, &discard) != 0) {
            return -1;
        }
        asm_label(head);
        if (condb[0]) {
            if (parse_from(condb, &discard) != 0) {
                return -1;
            }
            load_val(&discard);
            asm_line("cmp rax, 0");
            asm_cat("je ", end_lab, 0, 0);
        }
        if (parse_stmt() != 0) {
            return -1;
        }
        if (stepb[0] && parse_from(stepb, &discard) != 0) {
            return -1;
        }
        asm_cat("jmp ", head, 0, 0);
        asm_label(end_lab);
        return 0;
    }
    if (eat_kw("return")) {
        if (!eat_op(";")) {
            Val v;
            if (parse_expr(&v) != 0) {
                return -1;
            }
            load_val(&v);
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
        }
        epilogue();
        return 0;
    }
    if (eat_op(";")) {
        return 0;
    }
    if (peek_type_start()) {
        Type t;
        int is_static = 0;
        int is_inline = 0;
        char name[64];
        Sym s;
        if (parse_base(&t, &is_static, &is_inline) != 0) {
            return -1;
        }
        if (!take_ident(name, 64)) {
            return fail("declaration expected");
        }
        if (eat_op("[")) {
            uint64_t n = 0;
            int slen = 0;
            char lit[128];
            lit[0] = 0;
            if (!eat_op("]")) {
                if (!take_number(&n) || !eat_op("]")) {
                    return fail("bad array bound");
                }
            }
            t.pointee_size = t.size < 1 ? 1 : t.size;
            t.array_len = (int)n;
            if (eat_op("=")) {
                if (!take_string(lit, 128, &slen)) {
                    return fail("array initializer is not a string");
                }
                t.array_len = slen + 1;
                t.size = t.array_len * t.pointee_size;
            } else {
                t.size = t.array_len * t.pointee_size;
            }
            memset(&s, 0, sizeof(s));
            copy_str(s.name, 64, name);
            s.type = t;
            s.alive = 1;
            if (is_static) {
                char lab[16];
                emit_ro_string(lit, slen, lab);
                copy_str(s.asm_name, 64, lab);
                s.global = 1;
            } else {
                int off = alloc_slot(t.size);
                if (off == 0) {
                    return fail("frame is full");
                }
                s.frame = off;
                s.global = 0;
            }
            if (sym_add(&s) < 0) {
                return fail("too many symbols");
            }
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
            return 0;
        }
        memset(&s, 0, sizeof(s));
        copy_str(s.name, 64, name);
        s.type = t;
        if (is_static) {
            return fail("static scalar is outside this function shape");
        }
        s.frame = alloc_slot(8);
        if (s.frame == 0) {
            return fail("frame is full");
        }
        if (sym_add(&s) < 0) {
            return fail("too many symbols");
        }
        if (eat_op("=")) {
            Val init;
            if (parse_expr(&init) != 0) {
                return -1;
            }
            load_val(&init);
            store_rax_frame(s.frame);
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        return 0;
    }
    {
        Val v;
        if (parse_expr(&v) != 0) {
            return -1;
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        return 0;
    }
}

static int skip_braces(void) {
    int n = 0;
    while (*g_p) {
        if (*g_p == '"') {
            g_p++;
            while (*g_p && *g_p != '"') {
                if (*g_p == '\\' && g_p[1]) {
                    g_p++;
                }
                if (*g_p == '\n') {
                    g_line++;
                }
                g_p++;
            }
            if (*g_p == '"') {
                g_p++;
            }
            continue;
        }
        if (*g_p == '\'') {
            g_p++;
            while (*g_p && *g_p != '\'') {
                if (*g_p == '\\' && g_p[1]) {
                    g_p++;
                }
                g_p++;
            }
            if (*g_p == '\'') {
                g_p++;
            }
            continue;
        }
        if (*g_p == '{') {
            n++;
            g_p++;
            continue;
        }
        if (*g_p == '}') {
            n--;
            g_p++;
            if (n == 0) {
                return 0;
            }
            continue;
        }
        if (*g_p == '\n') {
            g_line++;
        }
        g_p++;
    }
    return fail("unclosed function");
}

static const char *arg_reg(int i) {
    static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    return regs[i];
}

static int parse_params(Sym *params, int *np) {
    *np = 0;
    if (eat_op(")")) {
        return 0;
    }
    if (eat_kw("void")) {
        if (eat_op(")")) {
            return 0;
        }
        return fail("bad parameter list");
    }
    do {
        Type t;
        int is_static = 0;
        int is_inline = 0;
        char name[64];
        if (*np >= 6) {
            return fail("too many parameters");
        }
        if (parse_base(&t, &is_static, &is_inline) != 0) {
            return -1;
        }
        if (!take_ident(name, 64)) {
            return fail("parameter name expected");
        }
        memset(&params[*np], 0, sizeof(params[*np]));
        copy_str(params[*np].name, 64, name);
        params[*np].type = t;
        params[*np].frame = alloc_slot(8);
        if (params[*np].frame == 0) {
            return fail("frame is full");
        }
        (*np)++;
    } while (eat_op(","));
    if (!eat_op(")")) {
        return fail("expected closing parenthesis");
    }
    return 0;
}

static int emit_global_bss(const char *name, int size, int is_static) {
    char num[16];
    if (size < 1) {
        size = 1;
    }
    u64_dec(num, (uint64_t)size);
    asm_line(".bss");
    if (is_static) {
        asm_line(".local");
    }
    asm_label(name);
    asm_cat(".zero ", num, 0, 0);
    asm_line(".text");
    return 0;
}

static int parse_global(void) {
    Type t;
    int is_static = 0;
    int is_inline = 0;
    char name[64];
    Sym s;
    int mark;
    if (parse_base(&t, &is_static, &is_inline) != 0) {
        return -1;
    }
    if (!take_ident(name, 64)) {
        return fail("declaration expected");
    }
    if (eat_op("[")) {
        uint64_t bound = 0;
        int elem;
        uint64_t bytes;
        if (!take_number(&bound) || bound == 0u || !eat_op("]")) {
            return fail("bad array bound");
        }
        if (!eat_op(";")) {
            return fail("global array initializer is outside this subset");
        }
        elem = t.size < 1 ? 1 : t.size;
        if (bound > (1ull << 20) / (uint64_t)elem) {
            return fail("array is too large");
        }
        bytes = bound * (uint64_t)elem;
        t.pointee_size = elem;
        t.size = (int)bytes;
        t.array_len = (int)bound;
        memset(&s, 0, sizeof(s));
        copy_str(s.name, 64, name);
        copy_str(s.asm_name, 64, name);
        s.type = t;
        s.global = 1;
        s.is_static = is_static;
        if (emit_global_bss(name, (int)bytes, is_static) != 0) {
            return -1;
        }
        if (sym_add(&s) < 0) {
            return fail("too many symbols");
        }
        return 0;
    }
    if (eat_op("(")) {
        Sym params[6];
        int np = 0;
        int i;
        int existing;
        g_frame = 0;
        if (parse_params(params, &np) != 0) {
            return -1;
        }
        if (eat_op(";")) {
            existing = sym_find(name);
            if (existing < 0) {
                memset(&s, 0, sizeof(s));
                copy_str(s.name, 64, name);
                copy_str(s.asm_name, 64, name);
                s.type = t;
                s.func = 1;
                s.global = 1;
                s.is_static = is_static;
                if (sym_add(&s) < 0) {
                    return fail("too many symbols");
                }
            }
            return 0;
        }
        if (!eat_op("{")) {
            return fail("expected function body");
        }
        if (is_inline) {
            g_p--;
            return skip_braces();
        }
        existing = sym_find(name);
        if (existing < 0) {
            memset(&s, 0, sizeof(s));
            copy_str(s.name, 64, name);
            copy_str(s.asm_name, 64, name);
            s.type = t;
            s.func = 1;
            s.global = 1;
            if (sym_add(&s) < 0) {
                return fail("too many symbols");
            }
        }
        asm_label(name);
        asm_line("push rbp");
        asm_line("mov rbp, rsp");
        asm_line("sub rsp, 512");
        mark = g_nsym;
        for (i = 0; i < np; i++) {
            if (sym_add(&params[i]) < 0) {
                return fail("too many symbols");
            }
            asm_cat("mov rax, ", arg_reg(i), 0, 0);
            store_rax_frame(params[i].frame);
        }
        while (!eat_op("}")) {
            if (parse_stmt() != 0) {
                return -1;
            }
        }
        epilogue();
        g_nsym = mark;
        return 0;
    }
    memset(&s, 0, sizeof(s));
    copy_str(s.name, 64, name);
    copy_str(s.asm_name, 64, name);
    s.type = t;
    s.global = 1;
    s.is_static = is_static;
    if (eat_op(";")) {
        if (emit_global_bss(name, t.size, is_static) != 0) {
            return -1;
        }
        if (sym_add(&s) < 0) {
            return fail("too many symbols");
        }
        return 0;
    }
    return fail("declaration is outside the level-0 subset");
}

static int compile_unit(void) {
    asm_line(".text");
    skip();
    while (*g_p) {
        skip();
        if (*g_p == 0) {
            break;
        }
        if (eat_kw("typedef")) {
            Type t;
            int is_static = 0;
            int is_inline = 0;
            char name[64];
            if (parse_base(&t, &is_static, &is_inline) != 0) {
                return -1;
            }
            if (!take_ident(name, 64)) {
                return fail("typedef name expected");
            }
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
            if (g_ntd >= KCC_TYPEDEF_MAX) {
                return fail("too many typedefs");
            }
            copy_str(g_td_name[g_ntd], 64, name);
            g_td_type[g_ntd] = t;
            g_ntd++;
            continue;
        }
        if (parse_global() != 0) {
            return -1;
        }
    }
    return 0;
}

int kcc_compile_named(const char *file, const char *src, ChrisoImage *out) {
    diag_clear();
    if (!src || !out) {
        return diag_error(file, 1, 1, "missing source");
    }
    g_asm_len = 0;
    g_asm_overflow = 0;
    g_pp_len = 0;
    g_nmac = 0;
    g_nstruct = 0;
    g_nsym = 0;
    g_ntd = 0;
    g_frame = 0;
    g_ntemp = 0;
    g_lab = 0;
    memset(g_mac, 0, sizeof(g_mac));
    memset(g_sym, 0, sizeof(g_sym));
    copy_str(g_file, 96, file ? file : "");
    g_line = 1;
    if (preprocess(file ? file : "", src, 0) != 0) {
        if (g_diag.severity == 0) {
            return fail("preprocessor directive");
        }
        return -1;
    }
    g_pp[g_pp_len] = 0;
    g_p = g_pp;
    copy_str(g_file, 96, file ? file : "");
    g_line = 1;
    if (compile_unit() != 0) {
        return -1;
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
