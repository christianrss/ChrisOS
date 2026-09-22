#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "cls.h"

#define MK_MAX_PATHS 128
#define MK_CODE_MAX (2u * 1024u * 1024u)
#define MK_FILE_MAX (CLVM_HEADER_SIZE_V2 + MK_CODE_MAX)

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

static int load_lst(const char *lst_path, const char **pp, char paths[][160],
                    int maxp, int *npaths_out) {
    static char lst[32768];
    FILE *f;
    size_t n;
    int npaths = 0;
    int p;
    f = fopen(lst_path, "rb");
    if (!f) {
        fprintf(stderr, "mk_clv: cannot open %s\n", lst_path);
        return 0;
    }
    n = fread(lst, 1, sizeof(lst) - 1u, f);
    fclose(f);
    lst[n] = 0;
    p = 0;
    while (p < (int)n && npaths < maxp) {
        while (p < (int)n && (lst[p] == ' ' || lst[p] == '\t' || lst[p] == '\r' ||
                              lst[p] == '\n'))
            p++;
        if (p >= (int)n)
            break;
        if (lst[p] == '#') {
            while (p < (int)n && lst[p] != '\n')
                p++;
            continue;
        }
        {
            int s = p;
            int i = 0;
            while (p < (int)n && lst[p] != '\n' && lst[p] != '\r')
                p++;
            while (p > s && (lst[p - 1] == ' ' || lst[p - 1] == '\t'))
                p--;
            if (p <= s)
                continue;
            while (s < p && i + 1 < 160) {
                paths[npaths][i++] = lst[s++];
            }
            paths[npaths][i] = 0;
            pp[npaths] = paths[npaths];
            npaths++;
        }
    }
    *npaths_out = npaths;
    return npaths > 0;
}

static void replace_ext(char *out, int cap, const char *lst, const char *ext) {
    int i = 0;
    int dot = -1;
    while (lst[i] && i + 1 < cap) {
        out[i] = lst[i];
        if (lst[i] == '.')
            dot = i;
        i++;
    }
    out[i] = 0;
    if (dot < 0)
        return;
    i = 0;
    while (ext[i] && dot + 1 + i < cap - 1) {
        out[dot + 1 + i] = ext[i];
        i++;
    }
    out[dot + 1 + i] = 0;
}

static int write_clv(const char *path, const ChrisResult *res,
                     const uint8_t *code) {
    static uint8_t file[MK_FILE_MAX];
    size_t n;
    FILE *f;
    uint32_t mem_hint = 0;
    if (res->code_size > 200000u)
        mem_hint = 32u * 1024u * 1024u;
    if (res->code_size > 65535u || res->entry > 65535u || mem_hint != 0)
        n = clvm_write_image_v2(file, sizeof(file), CLVM_FLAG_GAME, res->entry,
                                mem_hint, code, res->code_size);
    else
        n = clvm_write_image(file, sizeof(file), CLVM_FLAG_GAME,
                             (uint16_t)res->entry, code, res->code_size);
    if (!n) {
        fprintf(stderr, "mk_clv: write image failed\n");
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mk_clv: cannot write %s\n", path);
        return 0;
    }
    fwrite(file, 1, n, f);
    fclose(f);
    printf("mk_clv: %s (%u bytes code, mem_hint=%u)\n", path,
           (unsigned)res->code_size, (unsigned)mem_hint);
    return 1;
}

static int write_cls(const char *path, const char *libname,
                     const ChrisResult *res, const uint8_t *code) {
    ClsImage img;
    uint8_t file[65536 + 8192];
    size_t n;
    FILE *f;
    int i;
    memset(&img, 0, sizeof(img));
    strncpy(img.name, libname, CLS_NAME_MAX - 1);
    img.abi_major = res->abi_major ? res->abi_major : 1;
    img.abi_minor = res->abi_minor;
    img.code_size = (uint32_t)res->code_size;
    img.code = code;
    img.nexports = 0;
    for (i = 0; i < res->nexports && img.nexports < CLS_MAX_EXPORTS; ++i) {
        strncpy(img.exports[img.nexports].name, res->export_name[i],
                CLS_NAME_MAX - 1);
        img.exports[img.nexports].sym_ver = 1;
        img.exports[img.nexports].argc = res->export_argc[i];
        img.exports[img.nexports].pc = res->export_pc[i];
        img.exports[img.nexports].flags = 0;
        img.nexports++;
    }
    n = cls_write(file, sizeof(file), &img);
    if (!n) {
        fprintf(stderr, "mk_clv: cls_write failed\n");
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mk_clv: cannot write %s\n", path);
        return 0;
    }
    fwrite(file, 1, n, f);
    fclose(f);
    printf("mk_clv: %s abi %u.%u exports %d\n", path, img.abi_major,
           img.abi_minor, img.nexports);
    return 1;
}

int main(int argc, char **argv) {
    static char paths[MK_MAX_PATHS][160];
    const char *pp[MK_MAX_PATHS];
    static uint8_t *code;
    ChrisResult res;
    char outp[256];
    int npaths = 0;
    int as_cls = 0;
    const char *lst;
    const char *libname = "LIB";
    if (argc < 2) {
        fprintf(stderr, "usage: mk_clv <file.LST> [--cls name]\n");
        return 1;
    }
    lst = argv[1];
    if (argc >= 4 && strcmp(argv[2], "--cls") == 0) {
        as_cls = 1;
        libname = argv[3];
    }
    if (!load_lst(lst, pp, paths, MK_MAX_PATHS, &npaths))
        return 1;
    code = (uint8_t *)malloc(MK_CODE_MAX);
    if (!code) {
        fprintf(stderr, "mk_clv: out of memory\n");
        return 1;
    }
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile_files(pp, npaths, read_file, 0, code, MK_CODE_MAX,
                              &res)) {
        fprintf(stderr, "%s:%d:%d %s\n", res.diag.file, res.diag.line,
                res.diag.column, res.diag.message);
        free(code);
        return 1;
    }
    replace_ext(outp, (int)sizeof(outp), lst, as_cls ? "CLS" : "CLV");
    if (as_cls) {
        if (!write_cls(outp, libname, &res, code)) {
            free(code);
            return 1;
        }
    } else if (!write_clv(outp, &res, code)) {
        free(code);
        return 1;
    }
    free(code);
    return 0;
}
