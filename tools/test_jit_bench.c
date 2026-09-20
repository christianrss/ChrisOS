#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "chrisc/chrisc.h"
#include "clvm/clvm.h"
#include "clvm/clvm_vm.h"
#include "clvm_sys.h"
#include "gfx2d.h"
#include "jit/jit.h"
#include "jit/jit_compile.h"

#define BENCH_ROUNDS 400
#define BENCH_BUDGET 12000
#define MIN_SPEEDUP 5.0

static double bench_now(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER ctr;

    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    return (double)ctr.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static uint32_t g_fb[320 * 200];
static volatile uint32_t g_sink;

static int pixel_sys(ClvmVm *vm, int32_t id, void *user) {
    int32_t c;
    int32_t y;
    int32_t x;
    ClvmGfxCtx *ctx = (ClvmGfxCtx *)user;

    if (id != 1)
        return -1;
    if (!clvm_vm_pop(vm, &c) || !clvm_vm_pop(vm, &y) || !clvm_vm_pop(vm, &x))
        return -1;
    if (ctx != 0 && ctx->pixels != 0)
        gfx2d_put(ctx->pixels, ctx->w, ctx->h, x, y, c);
    g_sink ^= (uint32_t)x ^ (uint32_t)y ^ (uint32_t)c;
    return 0;
}

static uint32_t run_interp_bench(ClvmVm *vm, uint32_t rounds, uint32_t budget) {
    uint32_t i;
    uint32_t pixels = 0;

    for (i = 0; i < rounds; i++) {
        ClvmStepResult r = clvm_step(vm, budget);
        pixels += budget;
        if (r != CLVM_STEP_SLICE)
            break;
    }
    return pixels;
}

static uint32_t run_jit_bench(ClvmVm *vm, JitFn fn, uint32_t rounds, uint32_t budget) {
    uint32_t i;
    uint32_t pixels = 0;

    for (i = 0; i < rounds; i++) {
        ClvmStepResult r = fn(vm, budget, i);
        pixels += budget;
        if (r != CLVM_STEP_SLICE)
            break;
    }
    return pixels;
}

int main(void) {
    static const char *src =
        "void main(){\n"
        " int x;\n"
        " x=0;\n"
        " while(1){\n"
        "  pixel(x, 10, 12);\n"
        "  x=x+1;\n"
        "  if(x>200){ x=0; }\n"
        " }\n"
        "}\n";
    uint8_t code[4096];
    ChrisResult cr;
    ClvmImage image;
    ClvmVm vi;
    ClvmVm vj;
    ClvmGfxCtx gfx;
    JitBuf jb;
    JitFn fn;
    double t0;
    double t1;
    double interp_s;
    double jit_s;
    double speedup;
    uint32_t pi;
    uint32_t pj;

    memset(&jb, 0, sizeof(jb));
    memset(&gfx, 0, sizeof(gfx));
    gfx.pixels = g_fb;
    gfx.w = 320;
    gfx.h = 200;
    gfx.slot_id = -1;
    memset(g_fb, 0, sizeof(g_fb));

    if (chrisc_compile(src, strlen(src), code, sizeof(code), &cr) == 0) {
        fputs("test_jit_bench: compile failed\n", stderr);
        return 1;
    }
    image.version = CLVM_VERSION;
    image.flags = 0;
    image.entry = cr.entry;
    image.code_size = (uint32_t)cr.code_size;
    image.checksum = 0;
    image.code = code;

    if (jit_alloc(&jb) != 0 ||
        jit_compile_image(&image, &jb, &fn) != 0) {
        fputs("test_jit_bench: jit compile failed\n", stderr);
        return 1;
    }

    clvm_vm_init(&vi, &image, pixel_sys, &gfx);
    clvm_vm_init(&vj, &image, pixel_sys, &gfx);

    run_interp_bench(&vi, 8, BENCH_BUDGET);
    run_jit_bench(&vj, fn, 8, BENCH_BUDGET);
    clvm_vm_init(&vi, &image, pixel_sys, &gfx);
    clvm_vm_init(&vj, &image, pixel_sys, &gfx);

    t0 = bench_now();
    pi = run_interp_bench(&vi, BENCH_ROUNDS, BENCH_BUDGET);
    t1 = bench_now();
    interp_s = t1 - t0;

    t0 = bench_now();
    pj = run_jit_bench(&vj, fn, BENCH_ROUNDS, BENCH_BUDGET);
    t1 = bench_now();
    jit_s = t1 - t0;

    if (pi == 0 || pj == 0) {
        fputs("test_jit_bench: bench did not run\n", stderr);
        jit_free(&jb);
        return 1;
    }

    speedup = interp_s / jit_s;
    printf("test_jit_bench: interp=%.3fs jit=%.3fs speedup=%.2fx\n",
           interp_s, jit_s, speedup);

    jit_free(&jb);

    if (speedup < MIN_SPEEDUP) {
        fprintf(stderr, "test_jit_bench: need >= %.1fx, got %.2fx\n",
                MIN_SPEEDUP, speedup);
        return 1;
    }

    puts("test_jit_bench: ok");
    return 0;
}
