#include "shader/sh_pub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    n = ftell(f);
    if (n < 0 || n >= SH_SRC_MAX) {
        fclose(f);
        return 0;
    }
    rewind(f);
    b = (char *)malloc((size_t)n + 1u);
    if (!b) {
        fclose(f);
        return 0;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return 0;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

static int stage_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot && strcmp(dot, ".frag") == 0) {
        return SH_STAGE_FRAGMENT;
    }
    return SH_STAGE_VERTEX;
}

int main(int argc, char **argv) {
    int i;
    const char *path = 0;
    int dump_ast = 0;
    int dump_ir = 0;
    int dump_tgsi = 0;
    char *src;
    ShShader *s;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dump-ast") == 0) {
            dump_ast = 1;
        } else if (strcmp(argv[i], "--dump-ir") == 0) {
            dump_ir = 1;
        } else if (strcmp(argv[i], "--dump-tgsi") == 0) {
            dump_tgsi = 1;
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: cshader file.vert [--dump-ast] [--dump-ir] [--dump-tgsi]\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "usage: cshader file.vert [--dump-ast] [--dump-ir] [--dump-tgsi]\n");
        return 2;
    }
    src = read_file(path);
    if (!src) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    s = sh_compile(stage_of(path), path, src);
    free(src);
    if (!s) {
        fprintf(stderr, "compile failed\n");
        return 1;
    }
    if (!sh_shader_ok(s)) {
        fprintf(stderr, "%s", sh_shader_log(s));
        sh_shader_free(s);
        return 1;
    }
    if (dump_ast) {
        fputs(sh_shader_ast(s), stdout);
    }
    if (dump_ir) {
        fputs(sh_shader_ir(s), stdout);
    }
    if (dump_tgsi || (!dump_ast && !dump_ir)) {
        fputs(sh_shader_tgsi(s), stdout);
    }
    printf("cycles lex %llu parse %llu sem %llu ir %llu tgsi %llu\n",
           (unsigned long long)sh_shader_cycles(s, SH_STAT_LEX),
           (unsigned long long)sh_shader_cycles(s, SH_STAT_PARSE),
           (unsigned long long)sh_shader_cycles(s, SH_STAT_SEM),
           (unsigned long long)sh_shader_cycles(s, SH_STAT_IR),
           (unsigned long long)sh_shader_cycles(s, SH_STAT_TGSI));
    sh_shader_free(s);
    return 0;
}
