#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static int32_t rd32(const uint8_t *m, uint32_t off) {
    return (int32_t)((uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
                     ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24));
}

static int run_int(const char *src, int32_t expect, const char *tag) {
    uint8_t code[65536];
    ChrisResult res;
    ClvmVm vm;
    ClvmImage img;
    ClvmStepResult step;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "%s compile %d:%d %s\n", tag, res.diag.line, res.diag.column,
                res.diag.message);
        return 1;
    }
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.mem_hint = 0;
    img.header_size = 0;
    img.code = code;
    clvm_vm_init(&vm, &img, 0, 0);
    step = clvm_step(&vm, 2000000);
    if (step != CLVM_STEP_HALT && step != CLVM_STEP_YIELD) {
        fprintf(stderr, "%s run %d fault %d\n", tag, (int)step, (int)vm.fault);
        return 1;
    }
    if (rd32(vm.memory, 0) != expect) {
        fprintf(stderr, "%s got %d want %d\n", tag, (int)rd32(vm.memory, 0),
                (int)expect);
        return 1;
    }
    return 0;
}

int main(void) {
    if (run_int("void main(){ int a; a = -8 >> 1; }\n", -4, "sar"))
        return 1;
    if (run_int("void main(){ unsigned a; a = 0xFFFFFFFFu; a = a < 1u; }\n", 0,
                "ult"))
        return 1;
    if (run_int("void main(){ unsigned a; a = 1u; a = a <= 1u; }\n", 1, "ule"))
        return 1;
    if (run_int(
            "struct S { int a; int b; int c; };\n"
            "void main(){\n"
            " struct S x;\n"
            " struct S y;\n"
            " x.a = 1; x.b = 2; x.c = 3;\n"
            " y = x;\n"
            " x.a = y.b;\n"
            "}\n",
            2, "struct-copy"))
        return 1;
    if (run_int(
            "struct F { unsigned on : 1; unsigned wet : 1; int n; };\n"
            "void main(){\n"
            " int a;\n"
            " struct F f;\n"
            " f.on = 1;\n"
            " f.wet = 1;\n"
            " a = f.on + f.wet;\n"
            "}\n",
            2, "bitfield"))
        return 1;
    if (run_int(
            "void main(){\n"
            " int a;\n"
            " a = 0;\n"
            " for (long i = 1; i < 4; i = i + 1) a = a + i;\n"
            "}\n",
            6, "for-long"))
        return 1;
    if (run_int(
            "void main(){\n"
            " int a;\n"
            " a = 0;\n"
            " switch (2) {\n"
            "  case 1: a = 9; break;\n"
            "  default:\n"
            "   for (a = 0; a < 3; a = a + 1) {\n"
            "    if (a == 1) continue;\n"
            "   }\n"
            " }\n"
            "}\n",
            3, "continue-switch"))
        return 1;
    if (run_int(
            "struct S { int x; };\n"
            "void main(){\n"
            " struct S a;\n"
            " struct S *p[2];\n"
            " a.x = 7;\n"
            " p[0] = &a;\n"
            " a.x = p[0]->x;\n"
            "}\n",
            7, "ptr-array"))
        return 1;
    if (run_int("void main(){ int a; float f; f = 5.5 % 2.0; a = (int)f; }\n", 1,
                "fmod"))
        return 1;
    if (run_int(
            "void main(){\n"
            " int a;\n"
            " float _Complex z;\n"
            " z.re = 4.0;\n"
            " z.im = 9.0;\n"
            " a = (int)z.re;\n"
            "}\n",
            4, "complex"))
        return 1;
    if (run_int(
            "struct A { int x; int y; };\n"
            "void main(){\n"
            " int a;\n"
            " struct A v[2];\n"
            " v[0].x = 3;\n"
            " v[1].x = 4;\n"
            " a = v[0].x + v[1].x;\n"
            "}\n",
            7, "struct-array"))
        return 1;
    puts("test_chrisc_lang: ok");
    return 0;
}
