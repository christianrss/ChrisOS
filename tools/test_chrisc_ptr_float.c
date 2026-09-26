/*
 * Regression test for ChrisC pointer semantics (STABILITY_AUDIT CHRISC-01/02).
 *
 * A `float *` is a 64-bit pointer whose pointee is a float; it must NOT be
 * treated as a scalar float when passed as an argument, dereferenced, or
 * used in arithmetic. Two defects were fixed:
 *   1. function parameters of pointer type were flagged arg_float from the
 *      pointee type, truncating the pointer through the FSTORE/FLOAD path;
 *   2. `*p` (N_DEREF) never set is_float for a float pointee, so mixed
 *      float/int arithmetic inserted a spurious ITOF that reinterpreted the
 *      float bits as an integer.
 *
 * Results are written into a leading global array so the test does not depend
 * on the compiler's local-frame layout.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static uint32_t rd_u32(const uint8_t *m, uint32_t off) {
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
           ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

static float rd_f32(const uint8_t *m, uint32_t off) {
    union { uint32_t u; float f; } v;
    v.u = rd_u32(m, off);
    return v.f;
}

static int close(float a, float b) { return fabsf(a - b) < 1e-3f; }

static int run(const char *name, const char *src, ClvmVm *vm) {
    static uint8_t code[65536];
    ChrisResult res;
    ClvmImage img;
    ClvmStepResult step;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "%s: compile failed: %d:%d %s\n", name, res.diag.line,
                res.diag.column, res.diag.message);
        return 0;
    }
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.code = code;
    clvm_vm_init(vm, &img, 0, 0);
    step = clvm_step(vm, 1000000);
    if (step != CLVM_STEP_HALT && step != CLVM_STEP_YIELD) {
        fprintf(stderr, "%s: bad step result %d\n", name, (int)step);
        return 0;
    }
    return 1;
}

/* Canonical reproducer: pointer-to-float physics integration. g[0]=x, g[1]=vx. */
static const char src_move[] =
    "float g[4];\n"
    "struct Actor { float x; float vx; };\n"
    "void move(float *x, float *vx){\n"
    " *vx = *vx + 0.16;\n"
    " *x = *x + *vx;\n"
    " *vx = *vx * 0.72;\n"
    "}\n"
    "void main(){\n"
    " struct Actor a[1];\n"
    " a[0].x = 72.0;\n"
    " a[0].vx = 0.0;\n"
    " move(&a[0].x, &a[0].vx);\n"
    " g[0] = a[0].x;\n"
    " g[1] = a[0].vx;\n"
    "}\n";

/* Compound assignment through a float pointer. g[0]=result. */
static const char src_compound[] =
    "float g[4];\n"
    "void bump(float *p){\n"
    " *p += 2.5;\n"
    " *p *= 2.0;\n"
    "}\n"
    "void main(){\n"
    " float v[1];\n"
    " v[0] = 1.0;\n"
    " bump(&v[0]);\n"
    " g[0] = v[0];\n"
    "}\n";

/* Multiple float pointers. g[0]=result. */
static const char src_multi[] =
    "float g[4];\n"
    "void addto(float *dst, float *a, float *b){\n"
    " *dst = *a + *b;\n"
    "}\n"
    "void main(){\n"
    " float o[4];\n"
    " o[1] = 3.0;\n"
    " o[2] = 4.0;\n"
    " addto(&o[0], &o[1], &o[2]);\n"
    " g[0] = o[0];\n"
    "}\n";

/* int* must stay a 64-bit integer pointer. g[0]=result (read as int). */
static const char src_intptr[] =
    "int g[4];\n"
    "void inc(int *p){\n"
    " *p = *p + 5;\n"
    "}\n"
    "void main(){\n"
    " int n[1];\n"
    " n[0] = 37;\n"
    " inc(&n[0]);\n"
    " g[0] = n[0];\n"
    "}\n";

int main(void) {
    ClvmVm vm;

    if (!run("move", src_move, &vm)) return 1;
    {
        float x = rd_f32(vm.memory, 0);
        float vx = rd_f32(vm.memory, 4);
        printf("move: x=%.5f vx=%.5f (expect 72.16 / 0.1152)\n", x, vx);
        assert(close(x, 72.16f));
        assert(close(vx, 0.1152f));
    }

    if (!run("compound", src_compound, &vm)) return 1;
    {
        float v = rd_f32(vm.memory, 0);
        printf("compound: v=%.5f (expect 7.0)\n", v);
        assert(close(v, 7.0f));
    }

    if (!run("multi", src_multi, &vm)) return 1;
    {
        float out0 = rd_f32(vm.memory, 0);
        printf("multi: out=%.5f (expect 7.0)\n", out0);
        assert(close(out0, 7.0f));
    }

    if (!run("intptr", src_intptr, &vm)) return 1;
    {
        int32_t n = (int32_t)rd_u32(vm.memory, 0);
        printf("intptr: n=%d (expect 42)\n", n);
        assert(n == 42);
    }

    puts("test_chrisc_ptr_float: ok");
    return 0;
}
