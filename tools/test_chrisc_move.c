/*
 * Movement/damping integration test (STABILITY_AUDIT MINE-01).
 *
 * Mirrors the pointer-to-float physics core used by LIB/SIM.CC's sim_move
 * (velocity integration + 0.72 damping through float* out-parameters), which
 * the ChrisC float-pointer defect corrupted. Verifies:
 *   - holding "forward" for N frames produces finite, coherent forward motion;
 *   - releasing damps the velocity toward zero;
 *   - the untouched axis stays exactly zero (no cross-axis contamination);
 *   - no NaN/Inf is produced.
 *
 * This exercises the language/runtime level where the bug lived; it does not
 * pull in the voxel world or input stack (covered by the QEMU Mine smoke).
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

/* g[0]=z after holding, g[1]=vz after holding,
 * g[2]=z after release, g[3]=vz after release, g[4]=x (should stay 0). */
static const char src[] =
    "float g[8];\n"
    "void step(float *x, float *z, float *vx, float *vz, float ax, float az){\n"
    " *vx = *vx + ax;\n"
    " *vz = *vz + az;\n"
    " *x = *x + *vx;\n"
    " *z = *z + *vz;\n"
    " *vx = *vx * 0.72;\n"
    " *vz = *vz * 0.72;\n"
    "}\n"
    "void main(){\n"
    " float x[1]; float z[1]; float vx[1]; float vz[1];\n"
    " int i;\n"
    " x[0]=0.0; z[0]=0.0; vx[0]=0.0; vz[0]=0.0;\n"
    " i=0;\n"
    " while(i<8){ step(&x[0],&z[0],&vx[0],&vz[0], 0.0, 0.16); i=i+1; }\n"
    " g[0]=z[0]; g[1]=vz[0];\n"
    " i=0;\n"
    " while(i<40){ step(&x[0],&z[0],&vx[0],&vz[0], 0.0, 0.0); i=i+1; }\n"
    " g[2]=z[0]; g[3]=vz[0];\n"
    " g[4]=x[0];\n"
    "}\n";

int main(void) {
    static uint8_t code[65536];
    ChrisResult res;
    ClvmVm vm;
    ClvmImage img;
    ClvmStepResult step_r;
    float z_hold, vz_hold, z_rel, vz_rel, x_axis;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "compile failed: %d:%d %s\n", res.diag.line,
                res.diag.column, res.diag.message);
        return 1;
    }
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.code = code;
    clvm_vm_init(&vm, &img, 0, 0);
    step_r = clvm_step(&vm, 5000000);
    assert(step_r == CLVM_STEP_HALT || step_r == CLVM_STEP_YIELD);

    z_hold = rd_f32(vm.memory, 0);
    vz_hold = rd_f32(vm.memory, 4);
    z_rel = rd_f32(vm.memory, 8);
    vz_rel = rd_f32(vm.memory, 12);
    x_axis = rd_f32(vm.memory, 16);
    printf("hold: z=%.4f vz=%.4f | release: z=%.4f vz=%.4f | x=%.4f\n",
           z_hold, vz_hold, z_rel, vz_rel, x_axis);

    /* Finite everywhere. */
    assert(isfinite(z_hold) && isfinite(vz_hold));
    assert(isfinite(z_rel) && isfinite(vz_rel) && isfinite(x_axis));
    /* Holding forward accelerates and advances position. */
    assert(z_hold > 0.5f);
    assert(vz_hold > 0.05f);
    /* Releasing damps velocity toward zero (0.72^40 of a small value). */
    assert(vz_rel < vz_hold);
    assert(fabsf(vz_rel) < 1e-2f);
    /* Position keeps advancing while damping, then settles ahead of hold pos. */
    assert(z_rel >= z_hold);
    /* No cross-axis contamination: x untouched. */
    assert(x_axis == 0.0f);

    puts("test_chrisc_move: ok");
    return 0;
}
