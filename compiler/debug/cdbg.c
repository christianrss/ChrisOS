#include "debug/cdbg.h"
#include "chrisc/chrisc.h"

#include <string.h>

static int put_u8(uint8_t *p, int *n, int cap, unsigned v) {
    if (*n >= cap) {
        return 0;
    }
    p[*n] = (uint8_t)v;
    *n += 1;
    return 1;
}

static int put_u16(uint8_t *p, int *n, int cap, unsigned v) {
    return put_u8(p, n, cap, v & 0xffu) && put_u8(p, n, cap, (v >> 8) & 0xffu);
}

static int put_u32(uint8_t *p, int *n, int cap, unsigned v) {
    return put_u16(p, n, cap, v & 0xffffu) &&
           put_u16(p, n, cap, (v >> 16) & 0xffffu);
}

static int put_bytes(uint8_t *p, int *n, int cap, const char *s, int len) {
    int i;
    for (i = 0; i < len; ++i) {
        if (!put_u8(p, n, cap, (unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int rd_u16(const uint8_t *p, int n, int *i, unsigned *out) {
    if (*i + 2 > n) {
        return 0;
    }
    *out = (unsigned)p[*i] | ((unsigned)p[*i + 1] << 8);
    *i += 2;
    return 1;
}

static int rd_u32(const uint8_t *p, int n, int *i, unsigned *out) {
    unsigned lo;
    unsigned hi;
    if (!rd_u16(p, n, i, &lo) || !rd_u16(p, n, i, &hi)) {
        return 0;
    }
    *out = lo | (hi << 16);
    return 1;
}

static uint32_t fnv(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void copy_cap(char *d, int cap, const char *s, int n) {
    int i;
    if (cap <= 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    for (i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    d[n] = 0;
}

int cdbg_encode(uint8_t *out, int cap, const CdbgImage *img) {
    int n = 0;
    int i;
    if (!out || !img || cap < 28) {
        return -1;
    }
    if (img->nfiles < 0 || img->nfiles > CDBG_FILES || img->nlines < 0 ||
        img->nlines > CDBG_LINES || img->nfuncs < 0 || img->nfuncs > CDBG_FUNCS) {
        return -1;
    }
    if (!put_bytes(out, &n, cap, "CDBG", 4) || !put_u16(out, &n, cap, CDBG_VERSION) ||
        !put_u16(out, &n, cap, img->abi_major) ||
        !put_u16(out, &n, cap, img->abi_minor) || !put_u16(out, &n, cap, 0) ||
        !put_u32(out, &n, cap, img->exec_hash) ||
        !put_u32(out, &n, cap, img->source_hash) ||
        !put_u16(out, &n, cap, (unsigned)img->nfiles) ||
        !put_u16(out, &n, cap, (unsigned)img->nlines) ||
        !put_u16(out, &n, cap, (unsigned)img->nfuncs) ||
        !put_u16(out, &n, cap, 0)) {
        return -1;
    }
    for (i = 0; i < img->nfiles; ++i) {
        int len = 0;
        while (img->files[i].path[len] && len < CDBG_PATH - 1) {
            ++len;
        }
        if (!put_u16(out, &n, cap, img->files[i].id) ||
            !put_u16(out, &n, cap, (unsigned)len) ||
            !put_bytes(out, &n, cap, img->files[i].path, len)) {
            return -1;
        }
    }
    for (i = 0; i < img->nlines; ++i) {
        const CdbgLine *ln = &img->lines[i];
        if (!put_u32(out, &n, cap, ln->pc_start) ||
            !put_u32(out, &n, cap, ln->pc_end) ||
            !put_u16(out, &n, cap, ln->file_id) ||
            !put_u16(out, &n, cap, ln->line) ||
            !put_u16(out, &n, cap, ln->column) || !put_u16(out, &n, cap, 0)) {
            return -1;
        }
    }
    for (i = 0; i < img->nfuncs; ++i) {
        const CdbgFunc *fn = &img->funcs[i];
        int len = 0;
        while (fn->name[len] && len < CDBG_NAME - 1) {
            ++len;
        }
        if (!put_u16(out, &n, cap, (unsigned)len) ||
            !put_bytes(out, &n, cap, fn->name, len) ||
            !put_u32(out, &n, cap, fn->start) || !put_u32(out, &n, cap, fn->end) ||
            !put_u16(out, &n, cap, fn->file_id) ||
            !put_u16(out, &n, cap, fn->line) || !put_u16(out, &n, cap, fn->argc) ||
            !put_u16(out, &n, cap, 0)) {
            return -1;
        }
    }
    return n;
}

int cdbg_decode(const uint8_t *in, int n, CdbgImage *img) {
    int i = 0;
    int k;
    unsigned version;
    unsigned abi_maj;
    unsigned abi_min;
    unsigned flags;
    unsigned reserved;
    unsigned nfiles;
    unsigned nlines;
    unsigned nfuncs;
    unsigned exec_hash;
    unsigned source_hash;
    if (!in || !img || n < 28) {
        return -1;
    }
    if (in[0] != 'C' || in[1] != 'D' || in[2] != 'B' || in[3] != 'G') {
        return -1;
    }
    i = 4;
    if (!rd_u16(in, n, &i, &version) || !rd_u16(in, n, &i, &abi_maj) ||
        !rd_u16(in, n, &i, &abi_min) || !rd_u16(in, n, &i, &flags) ||
        !rd_u32(in, n, &i, &exec_hash) || !rd_u32(in, n, &i, &source_hash) ||
        !rd_u16(in, n, &i, &nfiles) || !rd_u16(in, n, &i, &nlines) ||
        !rd_u16(in, n, &i, &nfuncs) || !rd_u16(in, n, &i, &reserved)) {
        return -1;
    }
    (void)flags;
    (void)reserved;
    if (version != CDBG_VERSION || nfiles > CDBG_FILES || nlines > CDBG_LINES ||
        nfuncs > CDBG_FUNCS) {
        return -1;
    }
    memset(img, 0, sizeof(*img));
    img->version = (uint16_t)version;
    img->abi_major = (uint16_t)abi_maj;
    img->abi_minor = (uint16_t)abi_min;
    img->exec_hash = exec_hash;
    img->source_hash = source_hash;
    img->nfiles = (int)nfiles;
    img->nlines = (int)nlines;
    img->nfuncs = (int)nfuncs;
    for (k = 0; k < img->nfiles; ++k) {
        unsigned id;
        unsigned len;
        if (!rd_u16(in, n, &i, &id) || !rd_u16(in, n, &i, &len)) {
            return -1;
        }
        if (i + (int)len > n || len >= CDBG_PATH) {
            return -1;
        }
        img->files[k].id = (uint16_t)id;
        copy_cap(img->files[k].path, CDBG_PATH, (const char *)(in + i), (int)len);
        i += (int)len;
    }
    for (k = 0; k < img->nlines; ++k) {
        unsigned a;
        unsigned b;
        unsigned file;
        unsigned line;
        unsigned col;
        unsigned pad;
        if (!rd_u32(in, n, &i, &a) || !rd_u32(in, n, &i, &b) ||
            !rd_u16(in, n, &i, &file) || !rd_u16(in, n, &i, &line) ||
            !rd_u16(in, n, &i, &col) || !rd_u16(in, n, &i, &pad)) {
            return -1;
        }
        (void)pad;
        img->lines[k].pc_start = a;
        img->lines[k].pc_end = b;
        img->lines[k].file_id = (uint16_t)file;
        img->lines[k].line = (uint16_t)line;
        img->lines[k].column = (uint16_t)col;
    }
    for (k = 0; k < img->nfuncs; ++k) {
        unsigned len;
        unsigned start;
        unsigned end;
        unsigned file;
        unsigned line;
        unsigned argc;
        unsigned pad;
        if (!rd_u16(in, n, &i, &len)) {
            return -1;
        }
        if (i + (int)len > n || len >= CDBG_NAME) {
            return -1;
        }
        copy_cap(img->funcs[k].name, CDBG_NAME, (const char *)(in + i), (int)len);
        i += (int)len;
        if (!rd_u32(in, n, &i, &start) || !rd_u32(in, n, &i, &end) ||
            !rd_u16(in, n, &i, &file) || !rd_u16(in, n, &i, &line) ||
            !rd_u16(in, n, &i, &argc) || !rd_u16(in, n, &i, &pad)) {
            return -1;
        }
        (void)pad;
        img->funcs[k].start = start;
        img->funcs[k].end = end;
        img->funcs[k].file_id = (uint16_t)file;
        img->funcs[k].line = (uint16_t)line;
        img->funcs[k].argc = (uint16_t)argc;
    }
    return i;
}

int cdbg_line_at(const CdbgImage *img, uint32_t pc, CdbgLine *out) {
    int i;
    int found = -1;
    if (!img || !out) {
        return 0;
    }
    for (i = 0; i < img->nlines; ++i) {
        if (img->lines[i].pc_start <= pc) {
            found = i;
        } else {
            break;
        }
    }
    if (found < 0) {
        return 0;
    }
    *out = img->lines[found];
    return 1;
}

int cdbg_func_at(const CdbgImage *img, uint32_t pc, CdbgFunc *out) {
    int i;
    int found = -1;
    if (!img || !out) {
        return 0;
    }
    for (i = 0; i < img->nfuncs; ++i) {
        if (img->funcs[i].start <= pc &&
            (img->funcs[i].end == 0 || pc < img->funcs[i].end)) {
            found = i;
        }
    }
    if (found < 0) {
        return 0;
    }
    *out = img->funcs[found];
    return 1;
}

int cdbg_file_at(const CdbgImage *img, uint32_t pc, char *path, int cap) {
    CdbgLine line;
    int i;
    if (!cdbg_line_at(img, pc, &line) || !path || cap <= 0) {
        return 0;
    }
    for (i = 0; i < img->nfiles; ++i) {
        if (img->files[i].id == line.file_id) {
            copy_cap(path, cap, img->files[i].path, CDBG_PATH);
            return 1;
        }
    }
    path[0] = 0;
    return 0;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int cdbg_parse_map_line(const char *s, uint32_t *pc, uint16_t *line,
                        uint16_t *file) {
    int i = 0;
    unsigned p = 0;
    unsigned ln = 0;
    unsigned f = 0;
    int v;
    if (!s || !pc || !line || !file) {
        return 0;
    }
    while (s[i] == ' ' || s[i] == '\r' || s[i] == '\t') {
        ++i;
    }
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
    }
    v = hex_val(s[i]);
    if (v < 0) {
        return 0;
    }
    while ((v = hex_val(s[i])) >= 0) {
        p = (p << 4) | (unsigned)v;
        ++i;
    }
    while (s[i] == ' ') {
        ++i;
    }
    if (s[i] < '0' || s[i] > '9') {
        return 0;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        ln = ln * 10u + (unsigned)(s[i] - '0');
        ++i;
    }
    while (s[i] == ' ') {
        ++i;
    }
    if (s[i] >= '0' && s[i] <= '9') {
        while (s[i] >= '0' && s[i] <= '9') {
            f = f * 10u + (unsigned)(s[i] - '0');
            ++i;
        }
    }
    *pc = p;
    *line = (uint16_t)ln;
    *file = (uint16_t)f;
    return 1;
}

int cdbg_bound(const ChrisResult *result) {
    int n;
    int i;
    if (!result) {
        return 0;
    }
    n = 64;
    for (i = 0; i < result->nfiles && i < CHRIS_FILE_MAX; ++i) {
        n += 4 + 64;
    }
    n += result->map_n * 16;
    n += result->nexports * 48;
    return n + 64;
}

int cdbg_from_result(uint8_t *out, int cap, const ChrisResult *result,
                     const uint8_t *code, uint32_t code_n, uint32_t source_hash) {
    int n = 0;
    int i;
    int files;
    int lines;
    int funcs;
    uint32_t code_end;
    if (!out || !result || cap < 28) {
        return -1;
    }
    files = result->nfiles;
    if (files > CHRIS_FILE_MAX) {
        files = CHRIS_FILE_MAX;
    }
    if (files < 0) {
        files = 0;
    }
    lines = result->map_n;
    if (lines < 0) {
        lines = 0;
    }
    if (lines > 65535) {
        lines = 65535;
    }
    funcs = result->nexports;
    if (funcs > 64) {
        funcs = 64;
    }
    if (funcs < 0) {
        funcs = 0;
    }
    code_end = result->code_size > 0xffffffffu ? 0xffffffffu
                                               : (uint32_t)result->code_size;
    if (!put_bytes(out, &n, cap, "CDBG", 4) || !put_u16(out, &n, cap, CDBG_VERSION) ||
        !put_u16(out, &n, cap, result->abi_major) ||
        !put_u16(out, &n, cap, result->abi_minor) || !put_u16(out, &n, cap, 0) ||
        !put_u32(out, &n, cap, code && code_n ? fnv(code, code_n) : 0) ||
        !put_u32(out, &n, cap, source_hash) ||
        !put_u16(out, &n, cap, (unsigned)files) ||
        !put_u16(out, &n, cap, (unsigned)lines) ||
        !put_u16(out, &n, cap, (unsigned)funcs) || !put_u16(out, &n, cap, 0)) {
        return -1;
    }
    for (i = 0; i < files; ++i) {
        int len = 0;
        while (result->file_path[i][len] && len < 63) {
            ++len;
        }
        if (!put_u16(out, &n, cap, (unsigned)i) ||
            !put_u16(out, &n, cap, (unsigned)len) ||
            !put_bytes(out, &n, cap, result->file_path[i], len)) {
            return -1;
        }
    }
    for (i = 0; i < lines; ++i) {
        uint32_t end = code_end;
        if (i + 1 < lines) {
            end = result->map[i + 1].pc;
        }
        if (end < result->map[i].pc) {
            end = result->map[i].pc;
        }
        if (!put_u32(out, &n, cap, result->map[i].pc) ||
            !put_u32(out, &n, cap, end) ||
            !put_u16(out, &n, cap, result->map[i].file_id) ||
            !put_u16(out, &n, cap, result->map[i].line) ||
            !put_u16(out, &n, cap, 1) || !put_u16(out, &n, cap, 0)) {
            return -1;
        }
    }
    for (i = 0; i < funcs; ++i) {
        int len = 0;
        int k;
        uint32_t end = code_end;
        uint16_t file = 0;
        uint16_t line = 0;
        while (result->export_name[i][len] && len < 31) {
            ++len;
        }
        if (i + 1 < funcs && result->export_pc[i + 1] > result->export_pc[i]) {
            end = result->export_pc[i + 1];
        }
        for (k = 0; k < lines; ++k) {
            if (result->map[k].pc <= result->export_pc[i]) {
                file = result->map[k].file_id;
                line = result->map[k].line;
            }
        }
        if (!put_u16(out, &n, cap, (unsigned)len) ||
            !put_bytes(out, &n, cap, result->export_name[i], len) ||
            !put_u32(out, &n, cap, result->export_pc[i]) ||
            !put_u32(out, &n, cap, end) || !put_u16(out, &n, cap, file) ||
            !put_u16(out, &n, cap, line) ||
            !put_u16(out, &n, cap, result->export_argc[i]) ||
            !put_u16(out, &n, cap, 0)) {
            return -1;
        }
    }
    return n;
}
