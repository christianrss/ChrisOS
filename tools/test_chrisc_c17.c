#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static int sys_ok(ClvmVm *vm, int32_t id, void *user) {
    (void)vm;
    (void)id;
    (void)user;
    return -1;
}

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

static int wr32(uint8_t *p, int32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return 4;
}

static int compile_run(const char *src, const char *tag) {
    uint8_t code[65536];
    uint8_t file[70000];
    ChrisResult r;
    ClvmImage img;
    ClvmVm vm;
    size_t n;
    ClvmStepResult st;

    memset(&vm, 0, sizeof(vm));
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &r)) {
        fprintf(stderr, "%s compile fail %s:%d %s\n", tag, r.diag.file, r.diag.line,
                r.diag.message);
        return 1;
    }
    n = clvm_write_image_v2(file, sizeof(file), 0, r.entry, 0, code, r.code_size);
    if (!n) {
        fprintf(stderr, "%s image fail\n", tag);
        return 1;
    }
    if (clvm_parse(file, n, &img) != CL_LOAD_OK) {
        fprintf(stderr, "%s parse fail\n", tag);
        return 1;
    }
    clvm_vm_init(&vm, &img, sys_ok, 0);
    st = clvm_step(&vm, 2000000);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "%s run %d fault %d pc %u\n", tag, (int)st, (int)vm.fault,
                vm.pc);
        return 1;
    }
    return 0;
}

static int32_t rd32(const uint8_t *m, uint32_t off) {
    return (int32_t)((uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
                     ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24));
}

static int run_first_int(const char *src, int32_t expect, const char *tag) {
    uint8_t code[65536];
    uint8_t file[70000];
    ChrisResult r;
    ClvmImage img;
    ClvmVm vm;
    size_t n;
    ClvmStepResult st;
    int32_t got;

    memset(&vm, 0, sizeof(vm));
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &r)) {
        fprintf(stderr, "%s compile fail %s:%d %s\n", tag, r.diag.file, r.diag.line,
                r.diag.message);
        return 1;
    }
    n = clvm_write_image_v2(file, sizeof(file), 0, r.entry, 0, code, r.code_size);
    if (!n || clvm_parse(file, n, &img) != CL_LOAD_OK) {
        fprintf(stderr, "%s image fail\n", tag);
        return 1;
    }
    clvm_vm_init(&vm, &img, sys_ok, 0);
    st = clvm_step(&vm, 2000000);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "%s run %d fault %d pc %u\n", tag, (int)st, (int)vm.fault,
                vm.pc);
        return 1;
    }
    got = rd32(vm.memory, 0);
    if (got != expect) {
        fprintf(stderr, "%s got %d want %d\n", tag, (int)got, (int)expect);
        return 1;
    }
    return 0;
}

int main(void) {
    static const char *src =
        "#define N 3\n"
        "#if N > 2\n"
        "enum { A = 1, B };\n"
        "int add(int a, int b);\n"
        "int add(int a, int b){ return a+b; }\n"
        "void main(){\n"
        " int x;\n"
        " unsigned int u;\n"
        " long L;\n"
        " _Alignas(8) int y;\n"
        " int n;\n"
        " int a[4];\n"
        " n = 3;\n"
        " int vla[3];\n"
        " x = 1 << 3;\n"
        " x = x | 1;\n"
        " x = x & 15;\n"
        " x = x ^ 2;\n"
        " u = 1;\n"
        " L = 2;\n"
        " y = (1, 2);\n"
        " x = (int){4};\n"
        " a[0] = 0;\n"
        " a[1] = 9;\n"
        " vla[0] = 1;\n"
        " if (sizeof(long) != 8) { x = 0; }\n"
        " switch (A) { case 1: x = add(x, B); break; default: x = 0; }\n"
        " do { x = x + 1; } while (x < 0);\n"
        " x = _Generic(x, int: 1, float: 2, default: 0);\n"
        " _Static_assert(1, \"ok\");\n"
        "}\n"
        "#endif\n";
    static const char *src_desig =
        "struct S { int x; int y; };\n"
        "void main(){\n"
        " int a[4] = { [1] = 7, 8 };\n"
        " struct S s = { .y = 3 };\n"
        " s.x = a[1];\n"
        "}\n";
    static const char *src_a =
        "int g;\n"
        "int inc(int n){ return n+1; }\n";
    static const char *src_b =
        "void main(){ int x; x = inc(g); }\n";
    char many[1024];
    uint8_t *big;
    uint8_t *imgbuf;
    ChrisResult r;
    ClvmImage img;
    ClvmVm vm;
    size_t n;
    ClvmStepResult st;
    uint8_t code[65536];
    uint8_t file[70000];

    if (compile_run(src, "c17"))
        return 1;
    if (compile_run(src_desig, "desig"))
        return 1;

    many[0] = 0;
    strcat(many, src_a);
    strcat(many, src_b);
    if (compile_run(many, "cc-many"))
        return 1;

    {
        const char *pa = "build/host/tu_a.cc";
        const char *pb = "build/host/tu_b.cc";
        const char *ps[2];
        FILE *f;
        uint8_t code[65536];
        ChrisResult r;
        f = fopen(pa, "wb");
        if (!f)
            return 1;
        fputs("int inc(int n){ return n+2; }\n", f);
        fclose(f);
        f = fopen(pb, "wb");
        if (!f)
            return 1;
        fputs("void main(){ int x; x = inc(3); }\n", f);
        fclose(f);
        ps[0] = pa;
        ps[1] = pb;
        memset(&r, 0, sizeof(r));
        if (!chrisc_compile_files(ps, 2, read_file, 0, code, sizeof(code), &r)) {
            fprintf(stderr, "compile_files fail %s:%d %s\n", r.diag.file, r.diag.line,
                    r.diag.message);
            return 1;
        }
    }

    if (run_first_int(
            "void main(){\n"
            " int a;\n"
            " a = 2;\n"
            " switch(a){\n"
            "  case 1: a = 10; break;\n"
            "  case 2: a = 20; break;\n"
            "  default: a = 30;\n"
            " }\n"
            "}\n",
            20, "switch-match"))
        return 1;
    if (run_first_int(
            "void main(){\n"
            " int a;\n"
            " a = 9;\n"
            " switch(a){\n"
            "  case 1: a = 10; break;\n"
            "  default: a = 30;\n"
            " }\n"
            "}\n",
            30, "switch-default"))
        return 1;
    if (run_first_int("int g = 6;\nvoid main(){ }\n", 6, "global-init"))
        return 1;
    if (run_first_int(
            "struct S { int x; int y; };\n"
            "void main(){\n"
            " int a;\n"
            " struct S s;\n"
            " struct S *p;\n"
            " p = &s;\n"
            " p->x = 11;\n"
            " a = p->x;\n"
            "}\n",
            11, "arrow"))
        return 1;
    if (run_first_int(
            "int a;\n"
            "int inc(int v){ return v + 1; }\n"
            "void main(){\n"
            " long fp;\n"
            " fp = inc;\n"
            " a = fp(40);\n"
            "}\n",
            41, "calli"))
        return 1;
    if (run_first_int("void main(){ unsigned char a; a = 200; }\n", 200,
                      "unsigned-char"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(char)300; }\n", 44, "cast-char"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(char)-1; }\n", -1, "cast-schar"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(unsigned char)-1; }\n", 255,
                      "cast-uchar"))
        return 1;
    if (run_first_int("void main(){ int a; float f; f = (float)9; a = (int)f; }\n", 9,
                      "cast-float"))
        return 1;
    if (run_first_int("void main(){ int a; a = (_Bool)5; }\n", 1, "cast-bool"))
        return 1;
    if (run_first_int("void main(){ int a; a = (_Bool)0; }\n", 0, "cast-bool0"))
        return 1;
    if (run_first_int("void main(){ int a; a = ((void)1, 8); }\n", 8, "cast-void"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(long)42; }\n", 42, "cast-long"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(int *)0; }\n", 0, "cast-ptr"))
        return 1;
    if (run_first_int("typedef int I;\nvoid main(){ int a; a = (I)7; }\n", 7,
                      "cast-typedef"))
        return 1;
    if (run_first_int("void main(){ int a; a = (int)(int (*)(int))0; }\n", 0,
                      "cast-fnptr"))
        return 1;
    if (run_first_int(
            "void main(){\n"
            " int a;\n"
            " char *p;\n"
            " a = 4;\n"
            " p = (char *)&a;\n"
            " a = (int)*p;\n"
            "}\n",
            4, "cast-deref"))
        return 1;
    if (run_first_int("void main(){ int a; const int *p; a = 1; p = (const int *)&a; a = *p; }\n",
                      1, "cast-const-ptr"))
        return 1;
    if (compile_run("void main(){ int a; a = (int){4}; if (sizeof(char *) != 8) a = 0; }\n",
                    "sizeof-ptr"))
        return 1;

    memset(&r, 0, sizeof(r));
    {
        static const char *spr =
            "#include \"LIB/STDIO.CC\"\n"
            "void main(){\n"
            " char b[32];\n"
            " sprintf(b, \"%d\", 42);\n"
            "}\n";
        if (!chrisc_compile_ex("SRC/T.CC", spr, strlen(spr), read_file, 0, code,
                               sizeof(code), &r)) {
            fprintf(stderr, "sprintf compile fail %s:%d %s\n", r.diag.file,
                    r.diag.line, r.diag.message);
            return 1;
        }
        n = clvm_write_image_v2(file, sizeof(file), 0, r.entry, 0, code, r.code_size);
        assert(n != 0);
        assert(clvm_parse(file, n, &img) == CL_LOAD_OK);
        memset(&vm, 0, sizeof(vm));
        clvm_vm_init(&vm, &img, sys_ok, 0);
        st = clvm_step(&vm, 2000000);
        if (st != CLVM_STEP_HALT) {
            fprintf(stderr, "sprintf run %d fault %d\n", (int)st, (int)vm.fault);
            return 1;
        }
    }

    big = (uint8_t *)malloc(80000);
    imgbuf = (uint8_t *)malloc(80100);
    if (!big || !imgbuf) {
        return 1;
    }
    memset(big, CL_OP_NOP, 80000);
    big[0] = CL_OP_JMP32;
    wr32(big + 1, 70000 - 5);
    big[70000] = CL_OP_HALT;
    n = clvm_write_image_v2(imgbuf, 80100, 0, 0, 0, big, 70001);
    if (!n) {
        fprintf(stderr, "jmp32 image fail\n");
        return 1;
    }
    assert(clvm_parse(imgbuf, n, &img) == CL_LOAD_OK);
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, sys_ok, 0);
    st = clvm_step(&vm, 100);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "jmp32 run %d fault %d pc %u\n", (int)st, (int)vm.fault,
                vm.pc);
        return 1;
    }
    free(big);
    free(imgbuf);
    printf("test_chrisc_c17 ok\n");
    return 0;
}
