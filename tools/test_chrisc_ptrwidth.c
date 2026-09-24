/*
 * Pointer width. ChrisC `int` is 4 bytes and a cast to int shifts a 64-bit
 * value down to a signed 32-bit range. LIB/STRING.CC memmove used that cast
 * to choose the overlap direction, so an address at or above 2^31 compared
 * backwards. The library must compare the pointers themselves.
 */
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static uint32_t rd_u32(const uint8_t *m, uint32_t off) {
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
           ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

static int compile_run(const char *name, const char *src, ClvmVm *vm) {
    static uint8_t code[1 << 20];
    ChrisResult res;
    ClvmImage img;
    ClvmStepResult step;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "%s: %d:%d %s\n", name, res.diag.line, res.diag.column,
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
        fprintf(stderr, "%s: step %d fault %d\n", name, (int)step, vm->fault);
        return 0;
    }
    return 1;
}

static int has_int_narrow(const uint8_t *code, uint32_t n) {
    static const uint8_t pat[] = {0x01, 0x20, 0x00, 0x00, 0x00, 0x38,
                                  0x01, 0x20, 0x00, 0x00, 0x00, 0x3a};
    uint32_t i;
    if (n < sizeof(pat)) {
        return 0;
    }
    for (i = 0; i + sizeof(pat) <= n; i++) {
        if (memcmp(code + i, pat, sizeof(pat)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int extract_memmove(char *out, int cap) {
    FILE *f = fopen("LIB/STRING.CC", "rb");
    char buf[16384];
    size_t n;
    const char *start;
    const char *p;
    int depth;
    int len;
    if (!f) {
        fprintf(stderr, "cannot open LIB/STRING.CC\n");
        return -1;
    }
    n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = 0;
    start = strstr(buf, "void memmove(");
    if (!start) {
        fprintf(stderr, "memmove not found\n");
        return -1;
    }
    p = strchr(start, '{');
    if (!p) {
        return -1;
    }
    depth = 0;
    for (; *p; p++) {
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                p++;
                break;
            }
        }
    }
    len = (int)(p - start);
    if (len <= 0 || len + 1 > cap) {
        return -1;
    }
    memcpy(out, start, (size_t)len);
    out[len] = 0;
    return len;
}

int main(void) {
    ClvmVm vm;
    char memmove_src[4096];
    char unit[8192];
    static uint8_t code[1 << 20];
    ChrisResult res;
    static const char high[] =
        "int g[4];\n"
        "int cmp_ptr(char *d, char *s){\n"
        " if (d < s) return 1;\n"
        " return 0;\n"
        "}\n"
        "int cmp_narrow(char *d, char *s){\n"
        " int dl;\n"
        " int sl;\n"
        " dl = (int)d;\n"
        " sl = (int)s;\n"
        " if (dl < sl) return 1;\n"
        " return 0;\n"
        "}\n"
        "void main(){\n"
        " long hi;\n"
        " long lo;\n"
        " char *d;\n"
        " char *s;\n"
        " hi = 1;\n"
        " hi = hi << 31;\n"
        " lo = hi - 16;\n"
        " d = (char *)hi;\n"
        " s = (char *)lo;\n"
        " g[0] = cmp_ptr(d, s);\n"
        " g[1] = cmp_narrow(d, s);\n"
        "}\n";

    if (!compile_run("high", high, &vm)) {
        return 1;
    }
    if (rd_u32(vm.memory, 0) != 0u || rd_u32(vm.memory, 4) != 1u) {
        fprintf(stderr, "high bits: ptr=%u narrow=%u (expect 0 1)\n",
                rd_u32(vm.memory, 0), rd_u32(vm.memory, 4));
        return 1;
    }

    if (extract_memmove(memmove_src, (int)sizeof(memmove_src)) < 0) {
        return 1;
    }
    snprintf(unit, sizeof(unit),
             "int g[4];\n%s\n"
             "void main(){\n"
             " char buf[16];\n"
             " int i;\n"
             " i = 0;\n"
             " while (i < 16) { buf[i] = i + 65; i = i + 1; }\n"
             " memmove(buf + 4, buf, 8);\n"
             " g[0] = buf[4];\n"
             " g[1] = buf[11];\n"
             " memmove(buf, buf + 2, 6);\n"
             " g[2] = buf[0];\n"
             " g[3] = buf[5];\n"
             "}\n",
             memmove_src);
    if (!compile_run("overlap", unit, &vm)) {
        return 1;
    }
    if (rd_u32(vm.memory, 0) != 65u || rd_u32(vm.memory, 4) != 72u ||
        rd_u32(vm.memory, 8) != 67u || rd_u32(vm.memory, 12) != 68u) {
        fprintf(stderr, "overlap %u %u %u %u\n", rd_u32(vm.memory, 0),
                rd_u32(vm.memory, 4), rd_u32(vm.memory, 8),
                rd_u32(vm.memory, 12));
        return 1;
    }

    snprintf(unit, sizeof(unit), "int g[1];\n%s\nvoid main(){ g[0] = 1; }\n",
             memmove_src);
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(unit, strlen(unit), code, sizeof(code), &res)) {
        fprintf(stderr, "narrow scan: %s\n", res.diag.message);
        return 1;
    }
    if (has_int_narrow(code, res.code_size)) {
        fprintf(stderr, "memmove still narrows a pointer through int\n");
        return 1;
    }

    puts("test_chrisc_ptrwidth: ok");
    return 0;
}
