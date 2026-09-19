/* LEARN:SH16-15 — host driver for KCC */
#include "kcc.h"
#include "chriso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;

    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[sz] = 0;
    *out_len = sz;
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    ChrisoImage img;
    uint8_t out[65536];
    char *src;
    long slen;
    int n;
    const char *out_path;

    if (argc < 3) {
        fprintf(stderr, "usage: kcc input.c output.chriso\n");
        return 1;
    }
    src = read_file(argv[1], &slen);
    if (!src) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    if (kcc_compile_source(src, &img) != 0) {
        fprintf(stderr, "compile failed\n");
        free(src);
        return 1;
    }
    free(src);
    n = chriso_write(&img, out, sizeof(out));
    if (n < 0) {
        fprintf(stderr, "chriso_write failed\n");
        return 1;
    }
    out_path = argv[2];
    {
        FILE *f = fopen(out_path, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", out_path);
            return 1;
        }
        if (fwrite(out, 1, (size_t)n, f) != (size_t)n) {
            fclose(f);
            return 1;
        }
        fclose(f);
    }
    printf("kcc: wrote %s (%d bytes)\n", out_path, n);
    return 0;
}
