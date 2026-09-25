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
#define KCC_MAC_MAX 256
#define KCC_MAC_VAL 768
#define KCC_MAC_PARAM 4
#define KCC_STRUCT_MAX 80
#define KCC_PP_LINE 2048
#define KCC_FIELD_MAX 32
#define KCC_TYPEDEF_MAX 64
#define KCC_ENUM_MAX 512
#define KCC_INIT_MAX 4096
#define KCC_PARAM_MAX 16

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
    int inner_len;
    int struct_id;
    int size;
    int align;
    int is_volatile;
    int pointee_volatile;
    int is_func_ptr;
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
    char val[KCC_MAC_VAL];
    char param[KCC_MAC_PARAM][32];
    int nparam;
    int func;
    int used;
} Macro;

typedef struct Val {
    Type type;
    int lvalue;
    int func;
    int lv;
    int frame;
    int has_imm;
    uint64_t imm;
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
static int g_dead_ok;
static int g_dead_at;
static char g_dead_sym[64];
static int g_storage_extern;
static char g_break_lab[16];
static char g_cont_lab[16];
static int g_loop_depth;
static char g_enum_name[KCC_ENUM_MAX][64];
static uint64_t g_enum_val[KCC_ENUM_MAX];
static int g_nenum;

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
    memset(&g_mac[id], 0, sizeof(g_mac[id]));
    g_mac[id].used = 1;
    copy_str(g_mac[id].name, 64, name);
    copy_str(g_mac[id].val, KCC_MAC_VAL, val);
    return 0;
}

static int pp_expand_into(const char *text, int depth, char *dst, int cap, int *nlen);

static int pp_parse_args(const char *s, int i, char args[][384], int *nargs) {
    int depth = 0;
    int n = 0;
    int k = 0;
    int started = 0;
    if (s[i] != '(') {
        return -1;
    }
    i++;
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (s[i] == ')') {
        *nargs = 0;
        return i + 1;
    }
    while (s[i]) {
        char c = s[i];
        if (c == '"') {
            if (k + 1 >= 384) {
                return -1;
            }
            args[n][k++] = c;
            i++;
            while (s[i] && s[i] != '"') {
                if (k + 1 >= 384) {
                    return -1;
                }
                args[n][k++] = s[i++];
            }
            if (s[i] == '"') {
                if (k + 1 >= 384) {
                    return -1;
                }
                args[n][k++] = s[i++];
            }
            started = 1;
            continue;
        }
        if (c == '(') {
            depth++;
        } else if (c == ')') {
            if (depth == 0) {
                while (k > 0 && (args[n][k - 1] == ' ' || args[n][k - 1] == '\t')) {
                    k--;
                }
                args[n][k] = 0;
                *nargs = n + 1;
                return i + 1;
            }
            depth--;
        } else if (c == ',' && depth == 0) {
            while (k > 0 && (args[n][k - 1] == ' ' || args[n][k - 1] == '\t')) {
                k--;
            }
            args[n][k] = 0;
            n++;
            k = 0;
            started = 0;
            if (n >= KCC_MAC_PARAM) {
                return -1;
            }
            i++;
            while (s[i] == ' ' || s[i] == '\t') {
                i++;
            }
            continue;
        }
        if (!started && (c == ' ' || c == '\t')) {
            i++;
            continue;
        }
        if (k + 1 >= 384) {
            return -1;
        }
        args[n][k++] = c;
        started = 1;
        i++;
    }
    return -1;
}

static int pp_subst(const Macro *m, char args[][384], char *out, int cap) {
    const char *b = m->val;
    int n = 0;
    while (*b) {
        if (b[0] == '#' && b[1] == '#') {
            while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) {
                n--;
            }
            b += 2;
            while (*b == ' ' || *b == '\t') {
                b++;
            }
            continue;
        }
        if (is_ident_start(*b)) {
            char name[32];
            int k = 0;
            int p;
            int hit = 0;
            while (is_ident_ch(*b) && k + 1 < 32) {
                name[k++] = *b++;
            }
            name[k] = 0;
            for (p = 0; p < m->nparam; p++) {
                if (strcmp(m->param[p], name) == 0) {
                    const char *a = args[p];
                    hit = 1;
                    while (*a) {
                        if (n + 1 >= cap) {
                            return -1;
                        }
                        out[n++] = *a++;
                    }
                    break;
                }
            }
            if (!hit) {
                int t;
                for (t = 0; name[t]; t++) {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = name[t];
                }
            }
            continue;
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = *b++;
    }
    out[n] = 0;
    return 0;
}

static int pp_expand_into(const char *text, int depth, char *dst, int cap, int *nlen) {
    int i = 0;
    int n = 0;
    if (depth > 8) {
        while (text[i] && n + 1 < cap) {
            dst[n++] = text[i++];
        }
        dst[n] = 0;
        *nlen = n;
        return text[i] ? -1 : 0;
    }
    while (text[i]) {
        if (text[i] == '"') {
            if (n + 1 >= cap) {
                return -1;
            }
            dst[n++] = text[i++];
            while (text[i] && text[i] != '"') {
                if (n + 1 >= cap) {
                    return -1;
                }
                dst[n++] = text[i++];
            }
            if (text[i] == '"') {
                if (n + 1 >= cap) {
                    return -1;
                }
                dst[n++] = text[i++];
            }
            continue;
        }
        if (is_ident_start(text[i])) {
            char name[64];
            int k = 0;
            int id;
            while (is_ident_ch(text[i]) && k + 1 < 64) {
                name[k++] = text[i++];
            }
            name[k] = 0;
            id = macro_find(name);
            if (id >= 0 && g_mac[id].func) {
                int j = i;
                char raw[KCC_MAC_PARAM][384];
                char use[KCC_MAC_PARAM][384];
                char body[KCC_MAC_VAL];
                int nargs = 0;
                int next;
                int p;
                while (text[j] == ' ' || text[j] == '\t') {
                    j++;
                }
                if (text[j] != '(') {
                    id = -1;
                } else {
                    next = pp_parse_args(text, j, raw, &nargs);
                    if (next < 0 || nargs != g_mac[id].nparam) {
                        return -1;
                    }
                    for (p = 0; p < nargs; p++) {
                        int elen = 0;
                        if (strstr(g_mac[id].val, "##") != 0) {
                            copy_str(use[p], 384, raw[p]);
                        } else if (pp_expand_into(raw[p], depth + 1, use[p], 384, &elen) != 0) {
                            return -1;
                        }
                    }
                    if (pp_subst(&g_mac[id], use, body, KCC_MAC_VAL) != 0) {
                        return -1;
                    }
                    {
                        char nested[KCC_MAC_VAL];
                        int nn = 0;
                        if (pp_expand_into(body, depth + 1, nested, KCC_MAC_VAL, &nn) != 0) {
                            return -1;
                        }
                        if (n + nn >= cap) {
                            return -1;
                        }
                        memcpy(dst + n, nested, (size_t)nn);
                        n += nn;
                    }
                    i = next;
                    continue;
                }
            }
            if (id >= 0 && !g_mac[id].func) {
                char nested[KCC_MAC_VAL];
                int nn = 0;
                if (pp_expand_into(g_mac[id].val, depth + 1, nested, KCC_MAC_VAL, &nn) != 0) {
                    return -1;
                }
                if (n + nn >= cap) {
                    return -1;
                }
                memcpy(dst + n, nested, (size_t)nn);
                n += nn;
                continue;
            }
            if (n + k >= cap) {
                return -1;
            }
            memcpy(dst + n, name, (size_t)k);
            n += k;
            continue;
        }
        if (n + 1 >= cap) {
            return -1;
        }
        dst[n++] = text[i++];
    }
    dst[n] = 0;
    *nlen = n;
    return 0;
}

static int pp_expand_line(const char *line) {
    char buf[4096];
    int n = 0;
    if (pp_expand_into(line, 0, buf, 4096, &n) != 0) {
        return -2;
    }
    if (pp_puts(buf) != 0 || pp_putc('\n') != 0) {
        return -1;
    }
    return 0;
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
        /* Types are built in. These limits are the header's real macros. */
        if (strcmp(name, "stdint.h") == 0 || strcmp(name, "stddef.h") == 0) {
            if (macro_add("UINT8_MAX", "255u") != 0 ||
                macro_add("UINT16_MAX", "65535u") != 0 ||
                macro_add("UINT32_MAX", "4294967295u") != 0 ||
                macro_add("UINT64_MAX", "18446744073709551615ull") != 0 ||
                macro_add("SIZE_MAX", "18446744073709551615ull") != 0 ||
                macro_add("INT32_MAX", "2147483647") != 0 ||
                macro_add("INT64_MAX", "9223372036854775807ll") != 0) {
                return -1;
            }
        }
        return 0;
    }
#ifdef __freestanding__
    buf = (char *)kmalloc(65536u);
#else
    buf = (char *)malloc(65536u);
#endif
    if (!buf) {
        return -1;
    }
    rc = -1;
    if (quoted) {
        dir_of(from, dir, (int)sizeof(dir));
        if (join_path(path, (int)sizeof(path), dir, name) == 0) {
            rc = load_file(path, buf, 65536);
        }
    }
    if (rc < 0) {
        static const char *const incs[] = {
            "third_party/limine", "kernel/metal", "kernel/gfx", "kernel/wm",
            "kernel/tools",       "kernel/fs",    "kernel/lang", "kernel/net",
            "kernel/crypto",      "compiler",     "compiler/clvm", "compiler/jit",
            "compiler/chrisld",   "compiler/chrisasm", "compiler/kcc"
        };
        int i;
        for (i = 0; i < (int)(sizeof(incs) / sizeof(incs[0])); i++) {
            if (join_path(path, (int)sizeof(path), incs[i], name) != 0) {
                continue;
            }
            rc = load_file(path, buf, 65536);
            if (rc >= 0) {
                break;
            }
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
    char out[KCC_PP_LINE];
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

static int ce_expr(uint64_t *v);
static void skip(void);

static int pp_copy_quoted(const char **ps, char *out, int cap, int *nlen) {
    const char *s = *ps;
    int n = *nlen;
    char end = *s;
    if (n + 1 >= cap) {
        return -1;
    }
    out[n++] = *s++;
    while (*s && *s != end) {
        if (n + 1 >= cap) {
            return -1;
        }
        if (*s == '\\' && s[1]) {
            out[n++] = *s++;
            if (n + 1 >= cap) {
                return -1;
            }
        }
        out[n++] = *s++;
    }
    if (*s == end) {
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = *s++;
    }
    *ps = s;
    *nlen = n;
    return 0;
}

static int pp_rewrite_defined(const char *in, char *out, int cap) {
    int n = 0;
    const char *s = in;
    while (*s) {
        if (*s == '"' || *s == '\'') {
            if (pp_copy_quoted(&s, out, cap, &n) != 0) {
                return -1;
            }
            continue;
        }
        if (is_ident_start(*s)) {
            char name[64];
            int k = 0;
            const char *save = s;
            while (is_ident_ch(*s) && k + 1 < 64) {
                name[k++] = *s++;
            }
            name[k] = 0;
            if (strcmp(name, "defined") == 0) {
                const char *q = s;
                char id[64];
                int ik = 0;
                int paren = 0;
                const char *bit;
                while (*q == ' ' || *q == '\t') {
                    q++;
                }
                if (*q == '(') {
                    paren = 1;
                    q++;
                    while (*q == ' ' || *q == '\t') {
                        q++;
                    }
                }
                if (!is_ident_start(*q)) {
                    return -1;
                }
                while (is_ident_ch(*q) && ik + 1 < 64) {
                    id[ik++] = *q++;
                }
                id[ik] = 0;
                if (is_ident_ch(*q)) {
                    return -1;
                }
                while (*q == ' ' || *q == '\t') {
                    q++;
                }
                if (paren) {
                    if (*q != ')') {
                        return -1;
                    }
                    q++;
                }
                bit = macro_find(id) >= 0 ? "1" : "0";
                while (*bit) {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = *bit++;
                }
                s = q;
                continue;
            }
            while (save < s) {
                if (n + 1 >= cap) {
                    return -1;
                }
                out[n++] = *save++;
            }
            continue;
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = *s++;
    }
    out[n] = 0;
    return 0;
}

static int pp_idents_to_zero(const char *in, char *out, int cap) {
    int n = 0;
    const char *s = in;
    while (*s) {
        if (*s == '"' || *s == '\'') {
            if (pp_copy_quoted(&s, out, cap, &n) != 0) {
                return -1;
            }
            continue;
        }
        if (*s >= '0' && *s <= '9') {
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                if (n + 2 >= cap) {
                    return -1;
                }
                out[n++] = *s++;
                out[n++] = *s++;
                while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
                       (*s >= 'A' && *s <= 'F')) {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = *s++;
                }
            } else {
                while (*s >= '0' && *s <= '9') {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = *s++;
                }
            }
            while (*s == 'u' || *s == 'U' || *s == 'l' || *s == 'L') {
                if (n + 1 >= cap) {
                    return -1;
                }
                out[n++] = *s++;
            }
            continue;
        }
        if (is_ident_start(*s)) {
            while (is_ident_ch(*s)) {
                s++;
            }
            if (n + 1 >= cap) {
                return -1;
            }
            out[n++] = '0';
            continue;
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = *s++;
    }
    out[n] = 0;
    return 0;
}

static int pp_if_truth(const char *expr) {
    char rewritten[2048];
    char expanded[4096];
    char zeroed[4096];
    const char *saved;
    int saved_line;
    int elen = 0;
    uint64_t v = 0;
    int rc;
    if (pp_rewrite_defined(expr, rewritten, (int)sizeof(rewritten)) != 0) {
        return -1;
    }
    if (pp_expand_into(rewritten, 0, expanded, (int)sizeof(expanded), &elen) != 0) {
        return -1;
    }
    if (pp_idents_to_zero(expanded, zeroed, (int)sizeof(zeroed)) != 0) {
        return -1;
    }
    saved = g_p;
    saved_line = g_line;
    g_p = zeroed;
    rc = ce_expr(&v);
    if (rc == 0) {
        skip();
        if (*g_p != 0) {
            rc = -1;
        }
    }
    g_p = saved;
    g_line = saved_line;
    if (rc != 0) {
        return -1;
    }
    return v != 0u ? 1 : 0;
}

static int pp_read_logical(const char **ps, char *line, int cap, int *line_no) {
    const char *p = *ps;
    int n = 0;
    int saved_line = *line_no;
    if (*p == 0) {
        line[0] = 0;
        return 0;
    }
    while (*p) {
        if (*p == '\\') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (*q == '\r') {
                q++;
            }
            if (*q == '\n') {
                p = q + 1;
                (*line_no)++;
                continue;
            }
        }
        if (*p == '\n') {
            p++;
            (*line_no)++;
            break;
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        if (n + 1 >= cap) {
            *line_no = saved_line;
            return -1;
        }
        line[n++] = *p++;
    }
    line[n] = 0;
    *ps = p;
    return 1;
}

static int handle_directive(const char *file, const char *dir, int *skip, int *sp,
                            int *stack, int *taken, int depth, int *include_err) {
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
    if (strcmp(name, "ifndef") == 0 || strcmp(name, "ifdef") == 0 ||
        strcmp(name, "if") == 0) {
        char mname[64];
        int defined = 0;
        int active = 0;
        int k = 0;
        int parent = *skip;
        if (*sp >= 32) {
            return -1;
        }
        if (strcmp(name, "if") == 0) {
            if (!parent) {
                int ev = pp_if_truth(p);
                if (ev < 0) {
                    return -1;
                }
                active = ev;
            }
        } else {
            while (is_ident_ch(*p) && k + 1 < 64) {
                mname[k++] = *p++;
            }
            mname[k] = 0;
            defined = macro_find(mname) >= 0;
            if (!parent) {
                active = strcmp(name, "ifndef") == 0 ? !defined : defined;
            }
        }
        stack[*sp] = parent;
        taken[*sp] = parent ? 1 : active;
        (*sp)++;
        *skip = parent || !active;
        return 0;
    }
    if (strcmp(name, "elif") == 0) {
        int parent;
        if (*sp <= 0) {
            return -1;
        }
        parent = stack[*sp - 1];
        if (parent || taken[*sp - 1]) {
            *skip = 1;
            return 0;
        }
        {
            int ev = pp_if_truth(p);
            if (ev < 0) {
                return -1;
            }
            if (ev) {
                taken[*sp - 1] = 1;
                *skip = 0;
            } else {
                *skip = 1;
            }
        }
        return 0;
    }
    if (strcmp(name, "else") == 0) {
        int parent;
        if (*sp <= 0) {
            return -1;
        }
        parent = stack[*sp - 1];
        if (parent || taken[*sp - 1]) {
            *skip = 1;
        } else {
            taken[*sp - 1] = 1;
            *skip = 0;
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
        char val[KCC_MAC_VAL];
        char params[KCC_MAC_PARAM][32];
        int k = 0;
        int vi = 0;
        int is_func = 0;
        int nparam = 0;
        int id;
        int pi;
        while (is_ident_ch(*p) && k + 1 < 64) {
            mname[k++] = *p++;
        }
        mname[k] = 0;
        if (mname[0] == 0) {
            return -1;
        }
        if (*p == '(') {
            is_func = 1;
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != ')') {
                for (;;) {
                    int pk = 0;
                    if (nparam >= KCC_MAC_PARAM || !is_ident_start(*p)) {
                        return -1;
                    }
                    while (is_ident_ch(*p) && pk + 1 < 32) {
                        params[nparam][pk++] = *p++;
                    }
                    if (is_ident_ch(*p)) {
                        return -1;
                    }
                    params[nparam][pk] = 0;
                    nparam++;
                    while (*p == ' ' || *p == '\t') {
                        p++;
                    }
                    if (*p == ',') {
                        p++;
                        while (*p == ' ' || *p == '\t') {
                            p++;
                        }
                        continue;
                    }
                    break;
                }
            }
            if (*p != ')') {
                return -1;
            }
            p++;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p && *p != '\r') {
            if (vi + 1 >= KCC_MAC_VAL) {
                return -1;
            }
            val[vi++] = *p++;
        }
        while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t')) {
            vi--;
        }
        val[vi] = 0;
        if (macro_add(mname, val) != 0) {
            return -1;
        }
        if (is_func) {
            id = macro_find(mname);
            if (id < 0) {
                return -1;
            }
            g_mac[id].func = 1;
            g_mac[id].nparam = nparam;
            for (pi = 0; pi < nparam; pi++) {
                copy_str(g_mac[id].param[pi], 32, params[pi]);
            }
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
    if (strcmp(name, "error") == 0) {
        return -2;
    }
    if (strcmp(name, "pragma") == 0 || strcmp(name, "warning") == 0) {
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
    int taken[32];
    const char *p = src;
    if (depth > 16) {
        return -1;
    }
    while (*p) {
        char line[KCC_PP_LINE];
        int include_err = 0;
        int rc;
        int start_line = line_no;
        const char *probe;
        rc = pp_read_logical(&p, line, (int)sizeof(line), &line_no);
        if (rc < 0) {
            copy_str(g_file, 96, file);
            g_line = start_line;
            return fail("preprocessor line is too long");
        }
        if (rc == 0) {
            break;
        }
        strip_comments(line, &in_com);
        probe = line;
        while (*probe == ' ' || *probe == '\t') {
            probe++;
        }
        if (*probe == '#') {
            rc = handle_directive(file, probe, &skip, &sp, stack, taken, depth,
                                  &include_err);
            if (rc < 0) {
                copy_str(g_file, 96, file);
                g_line = start_line;
                if (include_err) {
                    return fail("cannot read include");
                }
                if (rc == -2) {
                    return fail("#error");
                }
                return fail("preprocessor directive");
            }
            continue;
        }
        if (!skip && !in_com && probe[0] != 0) {
            int er;
            if (pp_marker(file, start_line) != 0) {
                return fail("preprocessor buffer is full");
            }
            er = pp_expand_line(line);
            if (er == -2) {
                copy_str(g_file, 96, file);
                g_line = start_line;
                return fail("macro expansion failed");
            }
            if (er != 0) {
                return fail("preprocessor buffer is full");
            }
        }
    }
    if (sp != 0) {
        copy_str(g_file, 96, file);
        g_line = line_no;
        return fail("preprocessor directive");
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
            if (v > (18446744073709551615ull >> 4)) {
                return fail("integer constant overflow");
            }
            v = (v << 4) + (uint64_t)d;
            g_p++;
        }
    } else {
        while (*g_p >= '0' && *g_p <= '9') {
            unsigned d = (unsigned)(*g_p - '0');
            if (v > (18446744073709551615ull - d) / 10ull) {
                return fail("integer constant overflow");
            }
            v = v * 10ull + d;
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
    t.pointee_volatile = elem.is_volatile;
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

static int builtin_width(const char *name) {
    if (strcmp(name, "uint8_t") == 0 || strcmp(name, "int8_t") == 0) {
        return 1;
    }
    if (strcmp(name, "uint16_t") == 0 || strcmp(name, "int16_t") == 0) {
        return 2;
    }
    if (strcmp(name, "uint32_t") == 0 || strcmp(name, "int32_t") == 0) {
        return 4;
    }
    if (strcmp(name, "uint64_t") == 0 || strcmp(name, "int64_t") == 0 ||
        strcmp(name, "size_t") == 0 || strcmp(name, "ssize_t") == 0 ||
        strcmp(name, "uintptr_t") == 0 || strcmp(name, "intptr_t") == 0 ||
        strcmp(name, "ptrdiff_t") == 0) {
        return 8;
    }
    return 0;
}

static int is_type_name(const char *name) {
    if (strcmp(name, "void") == 0 || strcmp(name, "bool") == 0 ||
        strcmp(name, "char") == 0 || strcmp(name, "int") == 0 ||
        builtin_width(name) > 0) {
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
    if (strcmp(name, "uint64_t") == 0 || strcmp(name, "int64_t") == 0 ||
        strcmp(name, "size_t") == 0 || strcmp(name, "ssize_t") == 0 ||
        strcmp(name, "uintptr_t") == 0 || strcmp(name, "intptr_t") == 0 ||
        strcmp(name, "ptrdiff_t") == 0) {
        *t = type_make(TY_U64, 8, 8);
        return 1;
    }
    if (strcmp(name, "int8_t") == 0) {
        *t = type_make(TY_CHAR, 1, 1);
        return 1;
    }
    if (strcmp(name, "int16_t") == 0) {
        *t = type_make(TY_U16, 2, 2);
        return 1;
    }
    if (strcmp(name, "int32_t") == 0) {
        *t = type_make(TY_U32, 4, 4);
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

static int struct_find(const char *name) {
    int i;
    if (!name || !name[0]) {
        return -1;
    }
    for (i = 0; i < g_nstruct; i++) {
        if (strcmp(g_struct[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int enum_find(const char *name) {
    int i;
    for (i = 0; i < g_nenum; i++) {
        if (strcmp(g_enum_name[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int enum_add(const char *name, uint64_t val) {
    if (enum_find(name) >= 0 || g_nenum >= KCC_ENUM_MAX) {
        return -1;
    }
    copy_str(g_enum_name[g_nenum], 64, name);
    g_enum_val[g_nenum] = val;
    g_nenum++;
    return 0;
}

static int skip_attribute(int *packed) {
    const char *start;
    int depth;
    skip();
    if (strncmp(g_p, "__attribute__", 13) != 0) {
        return 0;
    }
    start = g_p;
    g_p += 13;
    skip();
    if (*g_p != '(') {
        return fail("bad attribute");
    }
    depth = 0;
    do {
        if (*g_p == '(') {
            depth++;
        } else if (*g_p == ')') {
            depth--;
        }
        if (*g_p == '\n') {
            g_line++;
        }
        g_p++;
    } while (*g_p && depth > 0);
    if (packed && !(*packed)) {
        const char *s;
        for (s = start; s + 6 < g_p; s++) {
            if (s[0] == 'p' && s[1] == 'a' && s[2] == 'c' && s[3] == 'k' &&
                s[4] == 'e' && s[5] == 'd') {
                *packed = 1;
            }
        }
    }
    return 1;
}

static int skip_decl_tail(void) {
    for (;;) {
        int rc;
        skip();
        if (strncmp(g_p, "__attribute__", 13) != 0) {
            return 0;
        }
        rc = skip_attribute(0);
        if (rc < 0) {
            return -1;
        }
    }
}

static int parse_struct_body(int id, int packed) {
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
        int is_volatile = 0;
        char tname[64];
        Field *f;
        int al;
        if (g_struct[id].nfield >= KCC_FIELD_MAX) {
            return fail("too many fields");
        }
        for (;;) {
            if (eat_kw("volatile")) {
                is_volatile = 1;
                continue;
            }
            if (eat_kw("const")) {
                continue;
            }
            if (eat_kw("unsigned")) {
                is_unsigned = 1;
                continue;
            }
            {
                int rc = skip_attribute(0);
                if (rc < 0) {
                    return -1;
                }
                if (rc > 0) {
                    continue;
                }
            }
            break;
        }
        if (eat_kw("struct")) {
            char tag[32];
            int sid;
            if (!take_ident(tag, 32)) {
                return fail("field type expected");
            }
            sid = struct_find(tag);
            if (sid < 0) {
                if (g_nstruct >= KCC_STRUCT_MAX) {
                    return fail("too many structs");
                }
                sid = g_nstruct++;
                memset(&g_struct[sid], 0, sizeof(g_struct[sid]));
                copy_str(g_struct[sid].name, 32, tag);
                g_struct[sid].align = 1;
            }
            ft = type_make(TY_STRUCT, g_struct[sid].size,
                           g_struct[sid].align < 1 ? 1 : g_struct[sid].align);
            ft.struct_id = sid;
        } else {
            const char *save = g_p;
            int line = g_line;
            if (!take_ident(tname, 64) || !type_from_name(tname, &ft)) {
                if (is_unsigned) {
                    g_p = save;
                    g_line = line;
                    ft = type_make(TY_U32, 4, 4);
                } else {
                    return fail("field type expected");
                }
            } else if (is_unsigned && ft.kind == TY_INT) {
                ft = type_make(TY_U32, 4, 4);
            }
        }
        (void)is_static;
        (void)is_inline;
        {
            Type base = ft;
            for (;;) {
                Type dt = base;
                if (g_struct[id].nfield >= KCC_FIELD_MAX) {
                    return fail("too many fields");
                }
                if (eat_op("(")) {
                    int depth;
                    if (!eat_op("*") || !take_ident(fname, 32) || !eat_op(")") || !eat_op("(")) {
                        return fail("field name expected");
                    }
                    depth = 1;
                    while (*g_p && depth > 0) {
                        if (*g_p == '(') {
                            depth++;
                        } else if (*g_p == ')') {
                            depth--;
                        }
                        if (*g_p == '\n') {
                            g_line++;
                        }
                        g_p++;
                    }
                    dt = type_ptr(dt);
                    dt.is_func_ptr = 1;
                } else {
                    while (eat_op("*")) {
                        dt = type_ptr(dt);
                    }
                    if (dt.kind == TY_STRUCT && !dt.is_ptr && dt.struct_id >= 0 &&
                        g_struct[dt.struct_id].nfield == 0 && g_struct[dt.struct_id].size == 0) {
                        return fail("field type expected");
                    }
                    if (!take_ident(fname, 32)) {
                        return fail("field name expected");
                    }
                    if (eat_op("[")) {
                        uint64_t bounds[2];
                        int nb = 0;
                        int scalar = dt.size < 1 ? 1 : dt.size;
                        uint64_t bytes;
                        do {
                            uint64_t bound = 0;
                            if (nb >= 2) {
                                return fail("bad array bound");
                            }
                            if (ce_expr(&bound) != 0) {
                                return -1;
                            }
                            if (bound == 0u || bound > 1000000u || !eat_op("]")) {
                                return fail("bad array bound");
                            }
                            bounds[nb++] = bound;
                        } while (eat_op("["));
                        bytes = (uint64_t)scalar;
                        if (nb == 2) {
                            bytes *= bounds[1];
                            dt.inner_len = (int)bounds[1];
                        }
                        bytes *= bounds[0];
                        if (bytes > 1000000u) {
                            return fail("array is too large");
                        }
                        dt.array_len = (int)bounds[0];
                        dt.pointee_size = (int)(bytes / bounds[0]);
                        dt.size = (int)bytes;
                    }
                }
                dt.is_volatile = is_volatile;
                al = packed ? 1 : (dt.align < 1 ? 1 : dt.align);
                off = (off + al - 1) & ~(al - 1);
                f = &g_struct[id].fields[g_struct[id].nfield++];
                memset(f, 0, sizeof(*f));
                copy_str(f->name, 32, fname);
                f->type = dt;
                f->offset = off;
                off += dt.size < 1 ? 1 : dt.size;
                if (al > max_align) {
                    max_align = al;
                }
                if (!eat_op(",")) {
                    break;
                }
            }
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
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
    int is_volatile = 0;
    char name[64];
    *is_static = 0;
    *is_inline = 0;
    g_storage_extern = 0;
    for (;;) {
        if (eat_kw("extern")) {
            g_storage_extern = 1;
            continue;
        }
        if (eat_kw("static")) {
            *is_static = 1;
            continue;
        }
        if (eat_kw("inline") || eat_kw("_Noreturn")) {
            *is_inline = 1;
            continue;
        }
        {
            int rc = skip_attribute(0);
            if (rc < 0) {
                return -1;
            }
            if (rc > 0) {
                continue;
            }
        }
        if (eat_kw("volatile")) {
            is_volatile = 1;
            continue;
        }
        if (eat_kw("const")) {
            continue;
        }
        if (eat_kw("unsigned")) {
            is_unsigned = 1;
            continue;
        }
        break;
    }
    if (eat_kw("struct")) {
        char tag[32];
        int has_tag = 0;
        int packed = 0;
        int id = -1;
        tag[0] = 0;
        if (skip_attribute(&packed) < 0) {
            return -1;
        }
        if (take_ident(tag, 32)) {
            has_tag = 1;
        }
        if (skip_attribute(&packed) < 0) {
            return -1;
        }
        skip();
        if (*g_p == '{') {
            if (has_tag) {
                id = struct_find(tag);
            }
            if (id < 0) {
                if (g_nstruct >= KCC_STRUCT_MAX) {
                    return fail("too many structs");
                }
                id = g_nstruct++;
                memset(&g_struct[id], 0, sizeof(g_struct[id]));
                if (has_tag) {
                    copy_str(g_struct[id].name, 32, tag);
                }
            }
            if (parse_struct_body(id, packed) != 0) {
                return -1;
            }
        } else if (has_tag) {
            id = struct_find(tag);
            if (id < 0) {
                if (g_nstruct >= KCC_STRUCT_MAX) {
                    return fail("too many structs");
                }
                id = g_nstruct++;
                memset(&g_struct[id], 0, sizeof(g_struct[id]));
                copy_str(g_struct[id].name, 32, tag);
                g_struct[id].align = 1;
            }
        } else {
            return fail("expected struct body");
        }
        *t = type_make(TY_STRUCT, g_struct[id].size, g_struct[id].align);
        t->struct_id = id;
        t->is_volatile = is_volatile;
        while (eat_op("*")) {
            *t = type_ptr(*t);
        }
        return 0;
    }
    skip();
    if (!is_ident_start(*g_p)) {
        if (is_unsigned) {
            *t = type_make(TY_U32, 4, 4);
            t->is_volatile = is_volatile;
            while (eat_op("*")) {
                *t = type_ptr(*t);
            }
            return 0;
        }
        return fail("type expected");
    }
    {
        const char *save = g_p;
        int line = g_line;
        if (!take_ident(name, 64) || !type_from_name(name, t)) {
            if (is_unsigned) {
                g_p = save;
                g_line = line;
                *t = type_make(TY_U32, 4, 4);
                t->is_volatile = is_volatile;
                while (eat_op("*")) {
                    *t = type_ptr(*t);
                }
                return 0;
            }
            return fail("type expected");
        }
    }
    if (is_unsigned && t->kind == TY_INT) {
        *t = type_make(TY_U32, 4, 4);
    }
    t->is_volatile = is_volatile;
    while (eat_op("*")) {
        *t = type_ptr(*t);
    }
    return 0;
}

static void lab_copy(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < 15) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
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
    if (g_frame + s > 1536) {
        return 0;
    }
    g_frame += s;
    return -g_frame;
}

static int temp_slot(void) {
    int off = -1600 - g_ntemp * 8;
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

static void dead_store_clear(void) {
    g_dead_ok = 0;
}

static void dead_store_drop(const char *sym) {
    int end;
    int n;
    if (!g_dead_ok || strcmp(g_dead_sym, sym) != 0) {
        return;
    }
    end = g_dead_at;
    if (end < 0 || end > g_asm_len) {
        g_dead_ok = 0;
        return;
    }
    while (end < g_asm_len && g_asm[end] != '\n') {
        end++;
    }
    if (end < g_asm_len && g_asm[end] == '\n') {
        end++;
    }
    n = g_asm_len - end;
    if (n > 0) {
        memmove(g_asm + g_dead_at, g_asm + end, (size_t)n);
    }
    g_asm_len = g_dead_at + n;
    g_dead_ok = 0;
}

static void dead_store_note(const char *sym) {
    copy_str(g_dead_sym, 64, sym);
    g_dead_at = g_asm_len;
    g_dead_ok = 1;
}

static void load_val(Val *v) {
    if (v->func) {
        return;
    }
    /* A literal is emitted while it is parsed. Later code may clobber rax
     * before this value is consumed, so reload it from the literal. */
    if (v->has_imm) {
        asm_mov_imm("rax", v->imm);
        v->has_imm = 0;
        v->lvalue = 0;
        v->lv = LV_NONE;
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
    dead_store_clear();
    if (v->lv == LV_LOCAL) {
        load_frame_rax(v->frame);
    } else if (v->lv == LV_GLOBAL) {
        if (v->type.is_volatile && v->type.size == 4 && !v->type.is_ptr) {
            asm_cat("mov eax, dword [rel ", v->gname, "]", 0);
        } else if (v->type.size <= 1 && !v->type.is_ptr) {
            asm_cat("movzx rax, byte [rel ", v->gname, "]", 0);
        } else {
            asm_cat("mov rax, [rel ", v->gname, "]", 0);
        }
    } else if (v->lv == LV_ADDR) {
        gen_addr(v);
        if (v->type.is_volatile && v->type.size == 4 && !v->type.is_ptr) {
            asm_line("mov rcx, rax");
            asm_line("mov eax, dword [rcx]");
        } else if (v->type.size <= 1 && !v->type.is_ptr) {
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
        if (dst->type.is_volatile) {
            dead_store_clear();
        } else {
            dead_store_drop(dst->gname);
            dead_store_note(dst->gname);
        }
        asm_cat("mov byte [rel ", dst->gname, "], al", 0);
        return;
    }
    if (dst->lv == LV_GLOBAL) {
        if (dst->type.is_volatile) {
            dead_store_clear();
            if (dst->type.size == 4 && !dst->type.is_ptr) {
                asm_cat("mov dword [rel ", dst->gname, "], eax", 0);
            } else {
                asm_cat("mov [rel ", dst->gname, "], rax", 0);
            }
            return;
        }
        dead_store_drop(dst->gname);
        dead_store_note(dst->gname);
        asm_cat("mov [rel ", dst->gname, "], rax", 0);
        return;
    }
    tmp = save_rax();
    gen_addr(dst);
    asm_line("mov rcx, rax");
    load_frame_rax(tmp);
    if (dst->type.is_volatile && dst->type.size == 4 && !dst->type.is_ptr) {
        asm_line("mov dword [rcx], eax");
    } else if (dst->type.size <= 1 && !dst->type.is_ptr) {
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
static int parse_postfix(Val *out);
static int postfix_tail(Val *out);
static int parse_sizeof_type(Type *t);

static int parse_primary(Val *out) {
    uint64_t num;
    char ident[64];
    char str[128];
    int slen = 0;
    memset(out, 0, sizeof(*out));
    out->type.array_len = -1;
    if (eat_kw("true")) {
        asm_mov_imm("rax", 1);
        out->has_imm = 1;
        out->imm = 1;
        out->type = type_make(TY_BOOL, 1, 1);
        return 0;
    }
    if (eat_kw("false")) {
        asm_mov_imm("rax", 0);
        out->has_imm = 1;
        out->imm = 0;
        out->type = type_make(TY_BOOL, 1, 1);
        return 0;
    }
    {
        int nr = take_number(&num);
        if (nr < 0) {
            return -1;
        }
        if (nr) {
            asm_mov_imm("rax", num);
            out->has_imm = 1;
            out->imm = num;
            out->type = type_make(TY_U64, 8, 8);
            return 0;
        }
    }
    if (take_char(&num)) {
        asm_mov_imm("rax", num);
        out->has_imm = 1;
        out->imm = num;
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
            int ev = enum_find(ident);
            if (ev >= 0) {
                asm_mov_imm("rax", g_enum_val[ev]);
                out->has_imm = 1;
                out->imm = g_enum_val[ev];
                out->type = type_make(TY_INT, 8, 8);
                return 0;
            }
            skip();
            if (*g_p == '(') {
                if (strncmp(ident, "__sync", 6) == 0 || strncmp(ident, "__atomic", 8) == 0 ||
                    strncmp(ident, "__builtin", 9) == 0) {
                    return fail("builtin is outside this subset");
                }
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

static int parse_args(const char *name, int indirect) {
    int slots[KCC_PARAM_MAX];
    int n = 0;
    static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int i;
    if (!eat_op(")")) {
        do {
            Val arg;
            if (n >= KCC_PARAM_MAX) {
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
    for (i = 0; i < n && i < 6; i++) {
        load_frame_rax(slots[i]);
        asm_cat("mov ", regs[i], ", rax", 0);
    }
    if (n > 6) {
        int extra = n - 6;
        int space = (extra + (extra & 1)) * 8;
        char num[16];
        u64_dec(num, (uint64_t)space);
        asm_cat("sub rsp, ", num, 0, 0);
        for (i = 0; i < extra; i++) {
            char mem[32];
            load_frame_rax(slots[6 + i]);
            mem[0] = '[';
            mem[1] = 'r';
            mem[2] = 's';
            mem[3] = 'p';
            if (i == 0) {
                mem[4] = ']';
                mem[5] = 0;
            } else {
                char off[16];
                int k = 0;
                u64_dec(off, (uint64_t)i * 8u);
                mem[4] = '+';
                while (off[k]) {
                    mem[5 + k] = off[k];
                    k++;
                }
                mem[5 + k] = ']';
                mem[6 + k] = 0;
            }
            asm_cat("mov ", mem, ", rax", 0);
        }
    }
    dead_store_clear();
    if (indirect) {
        load_frame_rax(indirect);
        asm_line("call rax");
    } else {
        asm_cat("call ", name, 0, 0);
    }
    if (n > 6) {
        int extra = n - 6;
        int space = (extra + (extra & 1)) * 8;
        char num[16];
        u64_dec(num, (uint64_t)space);
        asm_cat("add rsp, ", num, 0, 0);
    }
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

static int postfix_tail(Val *out) {
    for (;;) {
        if (eat_op("(")) {
            if (!out->func && !out->type.is_func_ptr) {
                return fail("call target is not a function");
            }
            if (out->type.is_func_ptr) {
                int slot;
                load_val(out);
                slot = save_rax();
                if (parse_args(0, slot) != 0) {
                    return -1;
                }
            } else if (parse_args(out->gname, 0) != 0) {
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
                elem_ty.size = elem;
                elem_ty.is_ptr = 0;
                if (out->type.inner_len > 0) {
                    elem_ty.array_len = out->type.inner_len;
                    elem_ty.inner_len = 0;
                    elem_ty.pointee_size = elem / out->type.inner_len;
                } else {
                    elem_ty.array_len = -1;
                }
            } else if (out->type.is_ptr) {
                elem = out->type.pointee_size < 1 ? 1 : out->type.pointee_size;
                elem_ty = type_make(out->type.kind, elem, elem < 8 ? elem : 8);
                elem_ty.is_volatile = out->type.pointee_volatile;
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
        {
            int arrow = 0;
            int is_mem = 0;
            char fname[32];
            Field *field = 0;
            int sid;
            int fi;
            if (g_p[0] == '-' && g_p[1] == '>') {
                arrow = 1;
                is_mem = 1;
                g_p += 2;
            } else if (g_p[0] == '.' && g_p[1] != '.') {
                is_mem = 1;
                g_p++;
            }
            if (!is_mem) {
                break;
            }
            if (arrow) {
                if (!out->type.is_ptr || out->type.struct_id < 0) {
                    return fail("arrow on a non-struct pointer");
                }
                sid = out->type.struct_id;
                load_val(out);
            } else {
                if (out->type.is_ptr || out->type.struct_id < 0) {
                    return fail("dot on a non-struct");
                }
                sid = out->type.struct_id;
                if (out->lvalue) {
                    gen_addr(out);
                }
            }
            if (!take_ident(fname, 32)) {
                return fail("field name expected");
            }
            if (sid < 0 || sid >= g_nstruct) {
                return fail("unknown field");
            }
            for (fi = 0; fi < g_struct[sid].nfield; fi++) {
                if (strcmp(g_struct[sid].fields[fi].name, fname) == 0) {
                    field = &g_struct[sid].fields[fi];
                    break;
                }
            }
            if (!field) {
                return fail("unknown field");
            }
            if (field->offset != 0) {
                asm_mov_imm("rcx", (uint64_t)field->offset);
                asm_line("add rax, rcx");
            }
            out->frame = save_rax();
            out->lv = LV_ADDR;
            out->lvalue = 1;
            out->func = 0;
            out->type = field->type;
            continue;
        }
    }
    return 0;
}

static int parse_unary(Val *out) {
    if (eat_kw("sizeof")) {
        Type t;
        if (parse_sizeof_type(&t) != 0) {
            return -1;
        }
        memset(out, 0, sizeof(*out));
        out->type.array_len = -1;
        out->has_imm = 1;
        out->imm = (uint64_t)t.size;
        asm_mov_imm("rax", (uint64_t)t.size);
        out->type = type_make(TY_U64, 8, 8);
        out->has_imm = 1;
        out->imm = (uint64_t)t.size;
        return 0;
    }
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
    skip();
    if (g_p[0] == '-' && g_p[1] != '-' && g_p[1] != '=' && g_p[1] != '>') {
        g_p++;
        if (parse_unary(out) != 0) {
            return -1;
        }
        load_val(out);
        asm_line("mov rcx, rax");
        asm_mov_imm("rax", 0);
        asm_line("sub rax, rcx");
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
    }
    if (g_p[0] == '~') {
        g_p++;
        if (parse_unary(out) != 0) {
            return -1;
        }
        load_val(out);
        asm_line("not rax");
        out->lvalue = 0;
        out->lv = LV_NONE;
        return 0;
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
            et.is_volatile = out->type.pointee_volatile;
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
        return postfix_tail(out);
    }
    return parse_postfix(out);
}

static int parse_postfix(Val *out) {
    if (parse_primary(out) != 0) {
        return -1;
    }
    return postfix_tail(out);
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
    if (eat_op("?")) {
        Val yes;
        Val no;
        char lno[16];
        char ldone[16];
        new_lab(lno);
        new_lab(ldone);
        load_val(out);
        asm_line("cmp rax, 0");
        asm_cat("je ", lno, 0, 0);
        if (parse_expr(&yes) != 0) {
            return -1;
        }
        if (!eat_op(":")) {
            return fail("expected colon");
        }
        load_val(&yes);
        asm_cat("jmp ", ldone, 0, 0);
        asm_label(lno);
        if (parse_expr(&no) != 0) {
            return -1;
        }
        load_val(&no);
        asm_label(ldone);
        out->lvalue = 0;
        out->lv = LV_NONE;
        out->type = no.type;
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
    if (eat_op("|=") || eat_op("&=") || eat_op("^=")) {
        Val right;
        Val saved;
        int ls;
        const char *op = "or rax, rcx";
        if (g_p[-2] == '&') {
            op = "and rax, rcx";
        } else if (g_p[-2] == '^') {
            op = "xor rax, rcx";
        }
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
        asm_line(op);
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

static int parse_sizeof_type(Type *t) {
    int paren;
    skip();
    paren = eat_op("(");
    if (paren && peek_type_start()) {
        int is_static = 0;
        int is_inline = 0;
        if (parse_base(t, &is_static, &is_inline) != 0) {
            return -1;
        }
        if (eat_op("[")) {
            uint64_t n = 0;
            int elem = t->size < 1 ? 1 : t->size;
            if (ce_expr(&n) != 0) {
                return -1;
            }
            if (n == 0u || !eat_op("]")) {
                return fail("bad array bound");
            }
            t->size = elem * (int)n;
        }
        if (!eat_op(")")) {
            return fail("expected closing parenthesis");
        }
        if (t->size < 1) {
            return fail("sizeof incomplete type");
        }
        return 0;
    }
    {
        int alen = g_asm_len;
        int aov = g_asm_overflow;
        int dok = g_dead_ok;
        int dat = g_dead_at;
        int ntemp = g_ntemp;
        int frame = g_frame;
        char dsym[64];
        Val v;
        memcpy(dsym, g_dead_sym, sizeof(dsym));
        if (paren) {
            if (parse_expr(&v) != 0) {
                return -1;
            }
            if (!eat_op(")")) {
                return fail("expected closing parenthesis");
            }
        } else if (parse_unary(&v) != 0) {
            return -1;
        }
        g_asm_len = alen;
        g_asm_overflow = aov;
        g_dead_ok = dok;
        g_dead_at = dat;
        g_ntemp = ntemp;
        g_frame = frame;
        memcpy(g_dead_sym, dsym, sizeof(dsym));
        *t = v.type;
        if (t->size < 1) {
            return fail("sizeof incomplete type");
        }
        return 0;
    }
}

static int ce_primary(uint64_t *v);
static int ce_unary(uint64_t *v);

static int ce_bin(uint64_t *v, int min_prec) {
    static const char *ops[] = {"||", "&&", "|",  "^",  "&",  "==", "!=", "<",  ">",
                                "<=", ">=", "<<", ">>", "+",  "-",  "*",  "/",  "%"};
    static const int prec_of[] = {1, 2, 3, 4, 5, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10};
    int nops = 18;
    if (ce_unary(v) != 0) {
        return -1;
    }
    for (;;) {
        int i;
        int matched = -1;
        skip();
        for (i = 0; i < nops; i++) {
            int n = (int)strlen(ops[i]);
            if (prec_of[i] < min_prec) {
                continue;
            }
            if (strncmp(g_p, ops[i], (size_t)n) != 0) {
                continue;
            }
            if (n == 1 && (g_p[1] == '=' || g_p[1] == ops[i][0])) {
                continue;
            }
            if (matched >= 0 && (int)strlen(ops[matched]) >= n) {
                continue;
            }
            matched = i;
        }
        if (matched < 0) {
            break;
        }
        g_p += strlen(ops[matched]);
        {
            uint64_t rhs;
            uint64_t lhs = *v;
            const char *op = ops[matched];
            if (ce_bin(&rhs, prec_of[matched] + 1) != 0) {
                return -1;
            }
            if (strcmp(op, "||") == 0) {
                *v = (lhs != 0u) || (rhs != 0u);
            } else if (strcmp(op, "&&") == 0) {
                *v = (lhs != 0u) && (rhs != 0u);
            } else if (strcmp(op, "|") == 0) {
                *v = lhs | rhs;
            } else if (strcmp(op, "^") == 0) {
                *v = lhs ^ rhs;
            } else if (strcmp(op, "&") == 0) {
                *v = lhs & rhs;
            } else if (strcmp(op, "==") == 0) {
                *v = lhs == rhs;
            } else if (strcmp(op, "!=") == 0) {
                *v = lhs != rhs;
            } else if (strcmp(op, "<") == 0) {
                *v = lhs < rhs;
            } else if (strcmp(op, ">") == 0) {
                *v = lhs > rhs;
            } else if (strcmp(op, "<=") == 0) {
                *v = lhs <= rhs;
            } else if (strcmp(op, ">=") == 0) {
                *v = lhs >= rhs;
            } else if (strcmp(op, "<<") == 0) {
                if (rhs >= 64u) {
                    return fail("constant expression expected");
                }
                *v = lhs << rhs;
            } else if (strcmp(op, ">>") == 0) {
                if (rhs >= 64u) {
                    return fail("constant expression expected");
                }
                *v = lhs >> rhs;
            } else if (strcmp(op, "+") == 0) {
                *v = lhs + rhs;
            } else if (strcmp(op, "-") == 0) {
                *v = lhs - rhs;
            } else if (strcmp(op, "*") == 0) {
                *v = lhs * rhs;
            } else if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
                if (rhs == 0u) {
                    return fail("constant expression expected");
                }
                *v = strcmp(op, "/") == 0 ? lhs / rhs : lhs % rhs;
            } else {
                return fail("constant expression expected");
            }
        }
    }
    return 0;
}

static int ce_primary(uint64_t *v) {
    uint64_t num;
    if (eat_kw("sizeof")) {
        Type t;
        if (parse_sizeof_type(&t) != 0) {
            return -1;
        }
        *v = (uint64_t)t.size;
        return 0;
    }
    {
        int nr = take_number(v);
        if (nr < 0) {
            return -1;
        }
        if (nr) {
            return 0;
        }
    }
    if (take_char(&num)) {
        *v = num;
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
            return ce_unary(v);
        }
        if (ce_bin(v, 0) != 0) {
            return -1;
        }
        if (!eat_op(")")) {
            return fail("expected closing parenthesis");
        }
        return 0;
    }
    {
        char name[64];
        const char *save = g_p;
        int line = g_line;
        if (take_ident(name, 64)) {
            int ev = enum_find(name);
            if (ev >= 0) {
                *v = g_enum_val[ev];
                return 0;
            }
            g_p = save;
            g_line = line;
        }
    }
    return fail("constant expression expected");
}

static int ce_unary(uint64_t *v) {
    skip();
    if (g_p[0] == '+' && g_p[1] != '+' && g_p[1] != '=') {
        g_p++;
        return ce_unary(v);
    }
    if (g_p[0] == '-' && g_p[1] != '-' && g_p[1] != '=' && g_p[1] != '>') {
        g_p++;
        if (ce_unary(v) != 0) {
            return -1;
        }
        *v = 0ull - *v;
        return 0;
    }
    if (g_p[0] == '~') {
        g_p++;
        if (ce_unary(v) != 0) {
            return -1;
        }
        *v = ~*v;
        return 0;
    }
    if (g_p[0] == '!' && g_p[1] != '=') {
        g_p++;
        if (ce_unary(v) != 0) {
            return -1;
        }
        *v = *v == 0u;
        return 0;
    }
    return ce_primary(v);
}

static int ce_expr(uint64_t *v) {
    return ce_bin(v, 0);
}

static int parse_static_assert(void) {
    uint64_t v = 0;
    char msg[128];
    int n = 0;
    if (!eat_op("(")) {
        return fail("bad static assert");
    }
    if (ce_expr(&v) != 0) {
        return -1;
    }
    if (!eat_op(",")) {
        return fail("bad static assert");
    }
    if (!take_string(msg, 128, &n)) {
        return fail("static assert message expected");
    }
    (void)msg;
    if (!eat_op(")") || !eat_op(";")) {
        return fail("bad static assert");
    }
    if (v == 0u) {
        return fail("static assert failed");
    }
    return 0;
}

static int constraint_has(const char *c, char ch) {
    while (*c) {
        if (*c == ch) {
            return 1;
        }
        c++;
    }
    return 0;
}

static int skip_asm_clobbers(void) {
    int depth = 0;
    while (*g_p) {
        if (*g_p == '(') {
            depth++;
        } else if (*g_p == ')') {
            if (depth == 0) {
                return 0;
            }
            depth--;
        } else if (*g_p == '\n') {
            g_line++;
        }
        g_p++;
    }
    return fail("bad asm");
}

static int parse_asm_operand(char *constraint, int ccap, Val *v) {
    char buf[32];
    int n = 0;
    skip();
    if (*g_p == '[') {
        return fail("asm operand is outside this subset");
    }
    if (!take_string(buf, 32, &n) || n < 1 || n >= ccap) {
        return fail("asm constraint expected");
    }
    copy_str(constraint, ccap, buf);
    if (!eat_op("(") || parse_expr(v) != 0 || !eat_op(")")) {
        return fail("asm operand expected");
    }
    return 0;
}

static int emit_port_io(int is_out, int width, Val *outs, const char oc[][8], int nout, Val *ins,
                        const char ic[][8], int nin) {
    int slot;
    if (is_out) {
        if (nout != 0 || nin != 2 || !constraint_has(ic[0], 'a')) {
            return fail("asm operands are outside this subset");
        }
        load_val(&ins[0]);
        slot = save_rax();
        load_val(&ins[1]);
        asm_line("mov rdx, rax");
        load_frame_rax(slot);
        if (width == 1) {
            asm_line("out dx, al");
        } else if (width == 2) {
            asm_line("out dx, ax");
        } else {
            asm_line("out dx, eax");
        }
        return 0;
    }
    if (nout != 1 || nin != 1 || !outs[0].lvalue || !constraint_has(oc[0], '=') ||
        !constraint_has(oc[0], 'a')) {
        return fail("asm operands are outside this subset");
    }
    load_val(&ins[0]);
    asm_line("mov rdx, rax");
    asm_line("xor rax, rax");
    if (width == 1) {
        asm_line("in al, dx");
    } else if (width == 2) {
        asm_line("in ax, dx");
    } else {
        asm_line("in eax, dx");
    }
    store_val(&outs[0]);
    return 0;
}

static int parse_asm_stmt(void) {
    char tmpl[160];
    int n = 0;
    int kind;
    int nout = 0;
    int nin = 0;
    Val outs[2];
    Val ins[2];
    char oc[2][8];
    char ic[2][8];
    if (eat_kw("volatile") || eat_kw("__volatile__")) {
        /* accepted; every asm in this subset is a compiler barrier */
    }
    if (!eat_op("(")) {
        return fail("bad asm");
    }
    if (!take_string(tmpl, 160, &n) || n >= 159) {
        return fail("asm template is outside this subset");
    }
    if (n < 159) {
        tmpl[n] = 0;
    }
    if (tmpl[0] == 0) {
        kind = 1;
    } else if (strcmp(tmpl, "cli") == 0) {
        kind = 2;
    } else if (strcmp(tmpl, "sti") == 0) {
        kind = 3;
    } else if (strcmp(tmpl, "hlt") == 0) {
        kind = 4;
    } else if (strcmp(tmpl, "pause") == 0) {
        kind = 5;
    } else if (strcmp(tmpl, "inb %1, %0") == 0) {
        kind = 10;
    } else if (strcmp(tmpl, "inw %1, %0") == 0) {
        kind = 11;
    } else if (strcmp(tmpl, "inl %1, %0") == 0) {
        kind = 12;
    } else if (strcmp(tmpl, "outb %0, %1") == 0) {
        kind = 13;
    } else if (strcmp(tmpl, "outw %0, %1") == 0) {
        kind = 14;
    } else if (strcmp(tmpl, "outl %0, %1") == 0) {
        kind = 15;
    } else {
        return fail("asm template is outside this subset");
    }
    if (eat_op(":")) {
        skip();
        if (*g_p != ':' && *g_p != ')') {
            do {
                if (nout >= 2) {
                    return fail("asm operands are outside this subset");
                }
                if (parse_asm_operand(oc[nout], 8, &outs[nout]) != 0) {
                    return -1;
                }
                nout++;
            } while (eat_op(","));
        }
        if (eat_op(":")) {
            skip();
            if (*g_p != ':' && *g_p != ')') {
                do {
                    if (nin >= 2) {
                        return fail("asm operands are outside this subset");
                    }
                    if (parse_asm_operand(ic[nin], 8, &ins[nin]) != 0) {
                        return -1;
                    }
                    nin++;
                } while (eat_op(","));
            }
            if (eat_op(":")) {
                if (skip_asm_clobbers() != 0) {
                    return -1;
                }
            }
        }
    }
    if (!eat_op(")") || !eat_op(";")) {
        return fail("bad asm");
    }
    dead_store_clear();
    if (kind == 1) {
        if (nout != 0 || nin != 0) {
            return fail("asm operands are outside this subset");
        }
        return 0;
    }
    if (kind >= 2 && kind <= 5) {
        if (nout != 0 || nin != 0) {
            return fail("asm operands are outside this subset");
        }
        if (kind == 2) {
            asm_line("cli");
        } else if (kind == 3) {
            asm_line("sti");
        } else if (kind == 4) {
            asm_line("hlt");
        } else {
            asm_line("pause");
        }
        return 0;
    }
    if (kind >= 10 && kind <= 12) {
        return emit_port_io(0, kind == 10 ? 1 : kind == 11 ? 2 : 4, outs, oc, nout, ins, ic, nin);
    }
    return emit_port_io(1, kind == 13 ? 1 : kind == 14 ? 2 : 4, outs, oc, nout, ins, ic, nin);
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
        if (!eat_op("(")) {
            return fail("bad if");
        }
        if (parse_expr(&cond) != 0) {
            return -1;
        }
        if (!eat_op(")")) {
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
    if (eat_kw("switch")) {
        Val v;
        int slot;
        char dispatch[16];
        char end_lab[16];
        char def_lab[16];
        char old_b[16];
        int old_d;
        int has_def = 0;
        uint64_t cvals[64];
        char clabs[64][16];
        int nc = 0;
        int i;
        if (!eat_op("(")) {
            return fail("bad switch");
        }
        if (parse_expr(&v) != 0) {
            return -1;
        }
        if (!eat_op(")") || !eat_op("{")) {
            return fail("bad switch");
        }
        load_val(&v);
        slot = alloc_slot(8);
        if (slot == 0) {
            return fail("frame is full");
        }
        store_rax_frame(slot);
        new_lab(dispatch);
        new_lab(end_lab);
        new_lab(def_lab);
        asm_cat("jmp ", dispatch, 0, 0);
        old_d = g_loop_depth;
        lab_copy(old_b, g_break_lab);
        lab_copy(g_break_lab, end_lab);
        g_loop_depth = old_d + 1;
        while (!eat_op("}")) {
            if (eat_kw("case")) {
                uint64_t cv = 0;
                char lab[16];
                if (nc >= 64) {
                    return fail("too many cases");
                }
                if (ce_expr(&cv) != 0) {
                    return -1;
                }
                if (!eat_op(":")) {
                    return fail("bad switch");
                }
                for (i = 0; i < nc; i++) {
                    if (cvals[i] == cv) {
                        return fail("duplicate case");
                    }
                }
                new_lab(lab);
                cvals[nc] = cv;
                lab_copy(clabs[nc], lab);
                nc++;
                asm_label(lab);
                continue;
            }
            if (eat_kw("default")) {
                if (has_def || !eat_op(":")) {
                    return fail("bad switch");
                }
                has_def = 1;
                asm_label(def_lab);
                continue;
            }
            if (parse_stmt() != 0) {
                return -1;
            }
        }
        asm_cat("jmp ", end_lab, 0, 0);
        asm_label(dispatch);
        for (i = 0; i < nc; i++) {
            char num[24];
            load_frame_rax(slot);
            u64_dec(num, cvals[i]);
            asm_cat("cmp rax, ", num, 0, 0);
            asm_cat("je ", clabs[i], 0, 0);
        }
        if (has_def) {
            asm_cat("jmp ", def_lab, 0, 0);
        }
        asm_label(end_lab);
        lab_copy(g_break_lab, old_b);
        g_loop_depth = old_d;
        return 0;
    }
    if (eat_kw("while")) {
        Val cond;
        char head[16];
        char end_lab[16];
        char old_b[16];
        char old_c[16];
        int old_d = g_loop_depth;
        lab_copy(old_b, g_break_lab);
        lab_copy(old_c, g_cont_lab);
        new_lab(head);
        new_lab(end_lab);
        lab_copy(g_break_lab, end_lab);
        lab_copy(g_cont_lab, head);
        g_loop_depth = old_d + 1;
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
        lab_copy(g_break_lab, old_b);
        lab_copy(g_cont_lab, old_c);
        g_loop_depth = old_d;
        return 0;
    }
    if (eat_kw("for")) {
        char initb[192];
        char condb[192];
        char stepb[192];
        char head[16];
        char step_lab[16];
        char end_lab[16];
        char old_b[16];
        char old_c[16];
        int old_d = g_loop_depth;
        Val discard;
        lab_copy(old_b, g_break_lab);
        lab_copy(old_c, g_cont_lab);
        new_lab(head);
        new_lab(step_lab);
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
        /* continue rechecks a while, but a for must run the step first. */
        lab_copy(g_break_lab, end_lab);
        lab_copy(g_cont_lab, step_lab);
        g_loop_depth = old_d + 1;
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
        asm_label(step_lab);
        if (stepb[0] && parse_from(stepb, &discard) != 0) {
            return -1;
        }
        asm_cat("jmp ", head, 0, 0);
        asm_label(end_lab);
        lab_copy(g_break_lab, old_b);
        lab_copy(g_cont_lab, old_c);
        g_loop_depth = old_d;
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
    if (eat_kw("break")) {
        if (g_loop_depth < 1) {
            return fail("break outside loop");
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        asm_cat("jmp ", g_break_lab, 0, 0);
        return 0;
    }
    if (eat_kw("continue")) {
        if (g_loop_depth < 1) {
            return fail("continue outside loop");
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        asm_cat("jmp ", g_cont_lab, 0, 0);
        return 0;
    }
    if (eat_kw("_Static_assert")) {
        return parse_static_assert();
    }
    if (eat_kw("__asm__") || eat_kw("asm")) {
        return parse_asm_stmt();
    }
    if (eat_kw("goto")) {
        char lab[64];
        if (!take_ident(lab, 64) || !eat_op(";")) {
            return fail("bad goto");
        }
        asm_cat("jmp ", lab, 0, 0);
        return 0;
    }
    if (eat_op(";")) {
        return 0;
    }
    {
        const char *save = g_p;
        int line = g_line;
        char lab[64];
        if (take_ident(lab, 64) && eat_op(":")) {
            asm_label(lab);
            return 0;
        }
        g_p = save;
        g_line = line;
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
                if (ce_expr(&n) != 0) {
                    return -1;
                }
                if (n == 0u || !eat_op("]")) {
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
    skip();
    if (strncmp(g_p, "void", 4) == 0 && !is_ident_ch(g_p[4])) {
        const char *save = g_p;
        int line = g_line;
        g_p += 4;
        skip();
        if (*g_p == ')') {
            g_p++;
            return 0;
        }
        g_p = save;
        g_line = line;
    }
    do {
        Type t;
        int is_static = 0;
        int is_inline = 0;
        char name[64];
        if (*np >= KCC_PARAM_MAX) {
            return fail("too many parameters");
        }
        if (parse_base(&t, &is_static, &is_inline) != 0) {
            return -1;
        }
        if (eat_op("(")) {
            int depth;
            if (!eat_op("*") || !take_ident(name, 64) || !eat_op(")") || !eat_op("(")) {
                return fail("parameter name expected");
            }
            depth = 1;
            while (*g_p && depth > 0) {
                if (*g_p == '(') {
                    depth++;
                } else if (*g_p == ')') {
                    depth--;
                }
                if (*g_p == '\n') {
                    g_line++;
                }
                g_p++;
            }
            t = type_ptr(t);
            t.is_func_ptr = 1;
        } else if (!take_ident(name, 64)) {
            return fail("parameter name expected");
        } else if (eat_op("[")) {
            skip();
            if (*g_p != ']') {
                uint64_t bound = 0;
                if (ce_expr(&bound) != 0) {
                    return -1;
                }
                (void)bound;
            }
            if (!eat_op("]")) {
                return fail("bad array bound");
            }
            t = type_ptr(t);
        }
        memset(&params[*np], 0, sizeof(params[*np]));
        copy_str(params[*np].name, 64, name);
        params[*np].type = t;
        if (*np < 6) {
            params[*np].frame = alloc_slot(8);
            if (params[*np].frame == 0) {
                return fail("frame is full");
            }
        } else {
            params[*np].frame = 16 + (*np - 6) * 8;
        }
        (*np)++;
    } while (eat_op(","));
    if (!eat_op(")")) {
        return fail("expected closing parenthesis");
    }
    return 0;
}

static int init_store(uint8_t *buf, int cap, int off, int size, uint64_t v) {
    int i;
    if (size < 1 || off < 0 || off > cap || size > cap - off) {
        return -1;
    }
    for (i = 0; i < size; i++) {
        buf[off + i] = (uint8_t)((v >> (8 * i)) & 0xffu);
    }
    return 0;
}

static Type init_elem(const Type *t) {
    Type e = *t;
    e.size = t->pointee_size < 1 ? 1 : t->pointee_size;
    e.array_len = -1;
    e.is_ptr = 0;
    if (e.size > 8) {
        e.align = 8;
    } else if (e.size > 0) {
        e.align = e.size;
    }
    return e;
}

static int parse_initializer(const Type *t, uint8_t *buf, int cap, int base) {
    if (eat_op("{")) {
        if (t->array_len > 0) {
            Type elem = init_elem(t);
            int i = 0;
            while (!eat_op("}")) {
                if (i >= t->array_len) {
                    return fail("bad initializer");
                }
                if (parse_initializer(&elem, buf, cap, base + i * elem.size) != 0) {
                    return -1;
                }
                i++;
                if (!eat_op(",")) {
                    if (!eat_op("}")) {
                        return fail("bad initializer");
                    }
                    break;
                }
            }
            return 0;
        }
        if (t->kind == TY_STRUCT && !t->is_ptr && t->struct_id >= 0 && t->array_len < 0) {
            StructDef *sd = &g_struct[t->struct_id];
            int next = 0;
            while (!eat_op("}")) {
                int fi;
                int f;
                if (eat_op(".")) {
                    char fname[32];
                    if (!take_ident(fname, 32) || !eat_op("=")) {
                        return fail("bad initializer");
                    }
                    fi = -1;
                    for (f = 0; f < sd->nfield; f++) {
                        if (strcmp(sd->fields[f].name, fname) == 0) {
                            fi = f;
                            break;
                        }
                    }
                    if (fi < 0) {
                        return fail("bad initializer");
                    }
                    if (parse_initializer(&sd->fields[fi].type, buf, cap,
                                          base + sd->fields[fi].offset) != 0) {
                        return -1;
                    }
                    next = fi + 1;
                } else {
                    if (next >= sd->nfield) {
                        return fail("bad initializer");
                    }
                    if (parse_initializer(&sd->fields[next].type, buf, cap,
                                          base + sd->fields[next].offset) != 0) {
                        return -1;
                    }
                    next++;
                }
                if (!eat_op(",")) {
                    if (!eat_op("}")) {
                        return fail("bad initializer");
                    }
                    break;
                }
            }
            return 0;
        }
        if (parse_initializer(t, buf, cap, base) != 0) {
            return -1;
        }
        if (!eat_op("}")) {
            return fail("bad initializer");
        }
        return 0;
    }
    skip();
    if (*g_p == '"') {
        char lit[1024];
        int slen = 0;
        int i;
        int room;
        if (!take_string(lit, 1024, &slen)) {
            return fail("bad initializer");
        }
        room = t->array_len > 0 ? t->array_len : t->size;
        if (room < 1) {
            room = slen + 1;
        }
        if (slen + 1 > room || base + room > cap) {
            return fail("bad initializer");
        }
        for (i = 0; i < slen; i++) {
            buf[base + i] = (uint8_t)lit[i];
        }
        buf[base + slen] = 0;
        return 0;
    }
    {
        uint64_t v = 0;
        int size = t->size < 1 ? 8 : t->size;
        if (t->is_ptr) {
            size = 8;
        }
        if (ce_expr(&v) != 0) {
            return -1;
        }
        if (init_store(buf, cap, base, size, v) != 0) {
            return fail("initializer is too large");
        }
    }
    return 0;
}

static int emit_global_bytes(const char *name, int is_static, const uint8_t *bytes, int n) {
    int i;
    asm_line(".data");
    if (is_static) {
        asm_line(".local");
    }
    asm_label(name);
    for (i = 0; i < n; i++) {
        char num[8];
        u64_dec(num, bytes[i]);
        asm_cat(".byte ", num, 0, 0);
    }
    asm_line(".text");
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

static int add_global_object(const char *name, Type t, int is_static, int is_extern,
                             const uint8_t *init, int init_n) {
    Sym s;
    memset(&s, 0, sizeof(s));
    copy_str(s.name, 64, name);
    copy_str(s.asm_name, 64, name);
    s.type = t;
    s.global = 1;
    s.is_static = is_static;
    if (is_extern) {
        asm_cat("extern ", name, 0, 0);
    } else if (init) {
        if (emit_global_bytes(name, is_static, init, init_n) != 0) {
            return -1;
        }
    } else if (emit_global_bss(name, t.size < 1 ? 1 : t.size, is_static) != 0) {
        return -1;
    }
    if (sym_add(&s) < 0) {
        return fail("too many symbols");
    }
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
        if (t.kind == TY_STRUCT && eat_op(";")) {
            return 0;
        }
        if (eat_op("(") && eat_op("*")) {
            uint64_t bound = 0;
            int has_bound = 0;
            int depth;
            if (!take_ident(name, 64)) {
                return fail("declaration expected");
            }
            if (eat_op("[")) {
                if (ce_expr(&bound) != 0) {
                    return -1;
                }
                if (bound == 0u || !eat_op("]")) {
                    return fail("bad array bound");
                }
                has_bound = 1;
            }
            if (!eat_op(")") || !eat_op("(")) {
                return fail("declaration expected");
            }
            depth = 1;
            while (*g_p && depth > 0) {
                if (*g_p == '(') {
                    depth++;
                } else if (*g_p == ')') {
                    depth--;
                }
                if (*g_p == '\n') {
                    g_line++;
                }
                g_p++;
            }
            if (skip_decl_tail() != 0) {
                return -1;
            }
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
            t = type_ptr(t);
            t.is_func_ptr = 1;
            t.pointee_size = 8;
            if (has_bound) {
                if (bound > (1ull << 20) / 8u) {
                    return fail("array is too large");
                }
                t.array_len = (int)bound;
                t.size = (int)bound * 8;
            }
            return add_global_object(name, t, is_static, g_storage_extern, 0, 0);
        }
        return fail("declaration expected");
    }
    if (eat_op("[")) {
        uint64_t bound = 0;
        int has_bound = 0;
        int elem;
        skip();
        if (*g_p != ']') {
            if (ce_expr(&bound) != 0) {
                return -1;
            }
            if (bound == 0u) {
                return fail("bad array bound");
            }
            has_bound = 1;
        }
        if (!eat_op("]")) {
            return fail("bad array bound");
        }
        if (skip_decl_tail() != 0) {
            return -1;
        }
        elem = t.size < 1 ? 1 : t.size;
        t.pointee_size = elem;
        if (g_storage_extern) {
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
            if (has_bound) {
                if (bound > (1ull << 20) / (uint64_t)elem) {
                    return fail("array is too large");
                }
                t.array_len = (int)bound;
                t.size = (int)(bound * (uint64_t)elem);
            } else {
                t.array_len = 0;
                t.size = elem;
            }
            return add_global_object(name, t, is_static, 1, 0, 0);
        }
        if (eat_op("=")) {
            uint8_t buf[KCC_INIT_MAX];
            skip();
            if (*g_p == '"') {
                char lit[1024];
                int slen = 0;
                int i;
                if (!take_string(lit, 1024, &slen)) {
                    return fail("bad initializer");
                }
                if (!has_bound) {
                    bound = (uint64_t)slen + 1u;
                    has_bound = 1;
                }
                if ((uint64_t)slen + 1u > bound || bound * (uint64_t)elem > KCC_INIT_MAX) {
                    return fail("bad initializer");
                }
                t.array_len = (int)bound;
                t.size = (int)(bound * (uint64_t)elem);
                memset(buf, 0, (size_t)t.size);
                for (i = 0; i < slen; i++) {
                    buf[i] = (uint8_t)lit[i];
                }
                if (!eat_op(";")) {
                    return fail("expected semicolon");
                }
                return add_global_object(name, t, is_static, 0, buf, t.size);
            }
            if (!has_bound) {
                return fail("bad array bound");
            }
            if (bound > (1ull << 20) / (uint64_t)elem) {
                return fail("array is too large");
            }
            t.array_len = (int)bound;
            t.size = (int)(bound * (uint64_t)elem);
            if (t.size < 1 || t.size > KCC_INIT_MAX) {
                return fail("initializer is too large");
            }
            memset(buf, 0, (size_t)t.size);
            if (parse_initializer(&t, buf, KCC_INIT_MAX, 0) != 0) {
                return -1;
            }
            if (!eat_op(";")) {
                return fail("expected semicolon");
            }
            return add_global_object(name, t, is_static, 0, buf, t.size);
        }
        if (!has_bound || !eat_op(";")) {
            return fail("bad array bound");
        }
        if (bound > (1ull << 20) / (uint64_t)elem) {
            return fail("array is too large");
        }
        t.array_len = (int)bound;
        t.size = (int)(bound * (uint64_t)elem);
        return add_global_object(name, t, is_static, 0, 0, 0);
    }
    if (eat_op("(")) {
        Sym params[KCC_PARAM_MAX];
        int np = 0;
        int i;
        int existing;
        g_frame = 0;
        if (parse_params(params, &np) != 0) {
            return -1;
        }
        if (skip_decl_tail() != 0) {
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
        asm_line("sub rsp, 2048");
        dead_store_clear();
        mark = g_nsym;
        for (i = 0; i < np; i++) {
            if (sym_add(&params[i]) < 0) {
                return fail("too many symbols");
            }
            if (i < 6) {
                asm_cat("mov rax, ", arg_reg(i), 0, 0);
                store_rax_frame(params[i].frame);
            }
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
    if (skip_decl_tail() != 0) {
        return -1;
    }
    if (eat_op("=")) {
        uint8_t buf[KCC_INIT_MAX];
        int n = t.is_ptr ? 8 : t.size;
        if (n < 1) {
            n = 8;
        }
        if (n > KCC_INIT_MAX) {
            return fail("initializer is too large");
        }
        t.size = n;
        memset(buf, 0, (size_t)n);
        if (parse_initializer(&t, buf, KCC_INIT_MAX, 0) != 0) {
            return -1;
        }
        if (!eat_op(";")) {
            return fail("expected semicolon");
        }
        return add_global_object(name, t, is_static, 0, buf, n);
    }
    if (!eat_op(";")) {
        return fail("declaration is outside the level-0 subset");
    }
    return add_global_object(name, t, is_static, g_storage_extern, 0, 0);
}

static int parse_enum(int is_typedef) {
    char name[64];
    uint64_t next = 0;
    skip();
    if (*g_p != '{') {
        if (!take_ident(name, 64)) {
            return fail("type expected");
        }
    }
    if (!eat_op("{")) {
        return fail("type expected");
    }
    while (!eat_op("}")) {
        char en[64];
        if (!take_ident(en, 64)) {
            return fail("type expected");
        }
        if (eat_op("=")) {
            uint64_t v = 0;
            if (ce_expr(&v) != 0) {
                return -1;
            }
            next = v;
        }
        if (enum_add(en, next) != 0) {
            return fail("too many symbols");
        }
        if (next != 18446744073709551615ull) {
            next++;
        }
        eat_op(",");
    }
    if (is_typedef) {
        Type td;
        if (!take_ident(name, 64) || !eat_op(";")) {
            return fail("typedef name expected");
        }
        if (g_ntd >= KCC_TYPEDEF_MAX) {
            return fail("too many typedefs");
        }
        td = type_make(TY_INT, 8, 8);
        copy_str(g_td_name[g_ntd], 64, name);
        g_td_type[g_ntd] = td;
        g_ntd++;
        return 0;
    }
    if (!eat_op(";")) {
        return fail("expected semicolon");
    }
    return 0;
}

static int compile_unit(void) {
    asm_line(".text");
    skip();
    while (*g_p) {
        skip();
        if (*g_p == 0) {
            break;
        }
        if (eat_op(";")) {
            continue;
        }
        if (eat_kw("enum")) {
            if (parse_enum(0) != 0) {
                return -1;
            }
            continue;
        }
        if (eat_kw("_Static_assert")) {
            if (parse_static_assert() != 0) {
                return -1;
            }
            continue;
        }
        if (eat_kw("typedef")) {
            Type t;
            int is_static = 0;
            int is_inline = 0;
            char name[64];
            if (eat_kw("enum")) {
                if (parse_enum(1) != 0) {
                    return -1;
                }
                continue;
            }
            if (parse_base(&t, &is_static, &is_inline) != 0) {
                return -1;
            }
            if (eat_op("(")) {
                int depth;
                if (!eat_op("*")) {
                    return fail("typedef name expected");
                }
                if (!take_ident(name, 64)) {
                    return fail("typedef name expected");
                }
                if (!eat_op(")") || !eat_op("(")) {
                    return fail("typedef name expected");
                }
                depth = 1;
                while (*g_p && depth > 0) {
                    if (*g_p == '(') {
                        depth++;
                    } else if (*g_p == ')') {
                        depth--;
                    }
                    if (*g_p == '\n') {
                        g_line++;
                    }
                    g_p++;
                }
                if (!eat_op(";")) {
                    return fail("expected semicolon");
                }
                t = type_ptr(t);
                t.is_func_ptr = 1;
                if (g_ntd >= KCC_TYPEDEF_MAX) {
                    return fail("too many typedefs");
                }
                copy_str(g_td_name[g_ntd], 64, name);
                g_td_type[g_ntd] = t;
                g_ntd++;
                continue;
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
    g_nenum = 0;
    g_frame = 0;
    g_ntemp = 0;
    g_lab = 0;
    g_dead_ok = 0;
    g_loop_depth = 0;
    g_break_lab[0] = 0;
    g_cont_lab[0] = 0;
    memset(g_mac, 0, sizeof(g_mac));
    memset(g_sym, 0, sizeof(g_sym));
    if (macro_add("__x86_64__", "1") != 0 || macro_add("__freestanding__", "1") != 0 ||
        macro_add("__VERSION__", "\"KCC\"") != 0 ||
        macro_add("LIMINE_API_REVISION", "3") != 0) {
        return diag_error(file, 1, 1, "preprocessor directive");
    }
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

const char *kcc_last_asm(void) {
    return g_asm;
}
