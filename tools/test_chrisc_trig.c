#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"
#include "math3d.h"

static const char src_float[] =
    "void main(){\n"
    " float a;\n"
    " float b;\n"
    " float c;\n"
    " a=90.0;\n"
    " b=0.0;\n"
    " c=sin(a)*cos(b);\n"
    "}\n";

static const char src_int[] =
    "void main(){\n"
    " float c;\n"
    " c=sin(90)*cos(0);\n"
    "}\n";

static uint32_t fbits(float f) {
    union {
        float fv;
        uint32_t u;
    } v;
    v.fv = f;
    return v.u;
}

static uint32_t rd_u32(const uint8_t *m, uint32_t off) {
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
           ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

static int trig_sys(ClvmVm *vm, int32_t id, void *user) {
    int32_t a;

    (void)user;
    if (id != 31 && id != 32)
        return -1;
    if (!clvm_vm_pop(vm, &a))
        return -1;
    if (id == 31)
        return clvm_vm_push(vm, (int32_t)gfx_sinf_bits((uint32_t)a)) ? 0 : -1;
    return clvm_vm_push(vm, (int32_t)gfx_cosf_bits((uint32_t)a)) ? 0 : -1;
}

static int code_has_fmul(const uint8_t *code, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (code[i] == CL_OP_FMUL)
            return 1;
    }
    return 0;
}

static int run_src(const char *src, uint32_t expect) {
    uint8_t code[4096];
    ChrisResult res;
    ClvmVm vm;
    ClvmImage img;
    ClvmStepResult step;
    uint32_t got;
    uint32_t off;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "compile failed: %d:%d %s\n", res.diag.line, res.diag.column,
                res.diag.message);
        return 0;
    }
    if (!code_has_fmul(code, res.code_size)) {
        fprintf(stderr, "expected FMUL in bytecode\n");
        return 0;
    }
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.code = code;
    clvm_vm_init(&vm, &img, trig_sys, 0);
    step = clvm_step(&vm, 100000);
    if (step != CLVM_STEP_HALT && step != CLVM_STEP_YIELD) {
        fprintf(stderr, "vm fault %d\n", (int)vm.fault);
        return 0;
    }
    for (off = 0; off <= 24; off += 4) {
        got = rd_u32(vm.memory, off);
        if (got == expect)
            return 1;
    }
    fprintf(stderr, "did not find expected float bits\n");
    return 0;
}

int main(void) {
    assert(run_src(src_float, fbits(1.0f)));
    assert(run_src(src_int, fbits(1.0f)));
    puts("test_chrisc_trig: ok");
    return 0;
}
