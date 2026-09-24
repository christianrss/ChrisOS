#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

#include <stdio.h>
#include <string.h>

static uint32_t rd_u32(const uint8_t *m, uint32_t off) {
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
           ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

static int compile_run(const char *src, ClvmVm *vm) {
    static uint8_t code[1 << 20];
    ChrisResult res;
    ClvmImage img;
    ClvmStepResult step;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "compile %d:%d %s\n", res.diag.line, res.diag.column,
                res.diag.message);
        return 0;
    }
    memset(&img, 0, sizeof(img));
    img.version = CLVM_VERSION;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.code = code;
    clvm_vm_init(vm, &img, 0, 0);
    step = clvm_step(vm, 2000000u);
    if (step != CLVM_STEP_HALT || vm->fault != CLVM_FAULT_NONE) {
        fprintf(stderr, "step %d fault %d\n", (int)step, vm->fault);
        return 0;
    }
    return 1;
}

static int expect(const ClvmVm *vm, int i, unsigned want, const char *what) {
    unsigned got = rd_u32(vm->memory, (uint32_t)i * 4u);
    if (got != want) {
        fprintf(stderr, "%s: g[%d]=%u want %u\n", what, i, got, want);
        return 0;
    }
    return 1;
}

static int algorithm(void) {
    static const char *src =
        "int g[16];\n"
        "char dst[97];\n"
        "char big[513];\n"
        "char srcb[640];\n"
        "void copy_cap(int d, int s, int cap) {\n"
        " int i;\n"
        " int c;\n"
        " i = 0;\n"
        " c = 1;\n"
        " while (i < cap - 1 && c != 0) {\n"
        "  c = loadb(s + i);\n"
        "  if (c != 0) {\n"
        "   storeb(d + i, c);\n"
        "   i = i + 1;\n"
        "  }\n"
        " }\n"
        " storeb(d + i, 0);\n"
        "}\n"
        "void copy_bug(int d, int s, int cap) {\n"
        " int i;\n"
        " i = 0;\n"
        " while (loadb(s + i) != 0) {\n"
        "  if (i < cap - 1) {\n"
        "   storeb(d + i, loadb(s + i));\n"
        "  }\n"
        "  i = i + 1;\n"
        " }\n"
        " storeb(d + i, 0);\n"
        "}\n"
        "void fill(int s, int n, int ch) {\n"
        " int i;\n"
        " i = 0;\n"
        " while (i < n) {\n"
        "  storeb(s + i, ch);\n"
        "  i = i + 1;\n"
        " }\n"
        " storeb(s + i, 0);\n"
        "}\n"
        "int slen(int s) {\n"
        " int i;\n"
        " i = 0;\n"
        " while (loadb(s + i) != 0) {\n"
        "  i = i + 1;\n"
        " }\n"
        " return i;\n"
        "}\n"
        "void main() {\n"
        " fill(srcb, 0, 65);\n"
        " copy_cap(dst, srcb, 96);\n"
        " g[0] = slen(dst);\n"
        " fill(srcb, 1, 69);\n"
        " copy_cap(dst, srcb, 96);\n"
        " g[1] = slen(dst);\n"
        " g[2] = loadb(dst);\n"
        " fill(srcb, 95, 65);\n"
        " copy_cap(dst, srcb, 96);\n"
        " g[3] = slen(dst);\n"
        " storeb(dst + 96, 77);\n"
        " fill(srcb, 96, 66);\n"
        " copy_cap(dst, srcb, 96);\n"
        " g[4] = slen(dst);\n"
        " g[5] = loadb(dst + 96);\n"
        " storeb(dst + 96, 77);\n"
        " fill(srcb, 96, 66);\n"
        " copy_bug(dst, srcb, 96);\n"
        " g[6] = loadb(dst + 96);\n"
        " fill(srcb, 511, 67);\n"
        " storeb(big + 512, 88);\n"
        " copy_cap(big, srcb, 512);\n"
        " g[7] = slen(big);\n"
        " g[8] = loadb(big + 512);\n"
        " fill(srcb, 512, 68);\n"
        " storeb(big + 512, 88);\n"
        " copy_cap(big, srcb, 512);\n"
        " g[9] = slen(big);\n"
        " g[10] = loadb(big + 512);\n"
        " fill(srcb, 600, 70);\n"
        " storeb(big + 512, 88);\n"
        " copy_cap(big, srcb, 512);\n"
        " g[11] = slen(big);\n"
        " g[12] = loadb(big + 512);\n"
        "}\n";
    ClvmVm vm;
    if (!compile_run(src, &vm)) {
        return 1;
    }
    if (!expect(&vm, 0, 0, "empty") || !expect(&vm, 1, 1, "one") ||
        !expect(&vm, 2, 69, "one char") || !expect(&vm, 3, 95, "95") ||
        !expect(&vm, 4, 95, "96 truncated") ||
        !expect(&vm, 5, 77, "canary after 96") ||
        !expect(&vm, 6, 0, "buggy nul") || !expect(&vm, 7, 511, "FS_PATH-1") ||
        !expect(&vm, 8, 88, "canary FS_PATH-1") ||
        !expect(&vm, 9, 511, "FS_PATH") ||
        !expect(&vm, 10, 88, "canary FS_PATH") ||
        !expect(&vm, 11, 511, "longer than FS_PATH") ||
        !expect(&vm, 12, 88, "canary longer")) {
        return 1;
    }
    return 0;
}

static int source_uses_bounded_copy(void) {
    FILE *f = fopen("APPS/EDITOR/EDITOR.CC", "rb");
    char buf[256 * 1024];
    size_t n;
    if (!f) {
        fprintf(stderr, "cannot open APPS/EDITOR/EDITOR.CC\n");
        return 1;
    }
    n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = 0;
    if (!strstr(buf, "copy_cap(g_path, p, 512)") ||
        !strstr(buf, "copy_cap(g_dir, p, 512)") ||
        !strstr(buf, "copy_cap(g_status, s, 80)") ||
        !strstr(buf, "char g_path[512]") || !strstr(buf, "char g_dir[512]")) {
        fprintf(stderr, "EDITOR.CC is missing the bounded path copy\n");
        return 1;
    }
    if (strstr(buf, "storeb(g_path + i, 0);") ||
        strstr(buf, "storeb(g_dir + i, 0);")) {
        fprintf(stderr, "EDITOR.CC still plants a NUL with an uncapped index\n");
        return 1;
    }
    return 0;
}

int main(void) {
    if (source_uses_bounded_copy() || algorithm()) {
        return 1;
    }
    puts("test_editor_path: ok");
    return 0;
}
