#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc/chrisc.h"
#include "clvm/clvm.h"
#include "clvm/clvm_vm.h"
#include "jit/jit.h"
#include "jit/jit_compile.h"

static int sys_count;
static int32_t sys_mem[16];

static int test_sys(ClvmVm *vm, int32_t id, void *user) {
    int32_t v;
    (void)user;
    sys_count++;
    if (id == 12) {
        clvm_vm_wait(vm, 1);
        return 0;
    }
    if (id == 99) {
        if (!clvm_vm_pop(vm, &v))
            return -1;
        sys_mem[0] = v;
        return 0;
    }
    return -1;
}

static void run_interp(ClvmVm *vm, uint32_t budget, uint32_t *steps) {
    ClvmStepResult r;
    uint32_t n = 0;
    for (;;) {
        r = clvm_step(vm, budget);
        n++;
        if (r == CLVM_STEP_YIELD)
            clvm_vm_wake(vm, 2);
        if (r != CLVM_STEP_SLICE)
            break;
    }
    *steps = n;
}

static void run_jit(ClvmVm *vm, JitFn fn, uint32_t budget, uint32_t *steps) {
    ClvmStepResult r;
    uint32_t n = 0;
    for (;;) {
        r = fn(vm, budget, n);
        n++;
        if (r == CLVM_STEP_YIELD)
            clvm_vm_wake(vm, 2);
        if (r != CLVM_STEP_SLICE)
            break;
    }
    *steps = n;
}

static int test_loop(void) {
    static const char *src =
        "void main(){\n"
        " int a;\n"
        " a=0;\n"
        " while(1){\n"
        "  a=a+1;\n"
        "  if(a>3){ a=0; }\n"
        "  wait(1);\n"
        " }\n"
        "}\n";
    uint8_t code[4096];
    ChrisResult cr;
    ClvmImage image;
    ClvmVm vi;
    ClvmVm vj;
    JitBuf jb;
    JitFn fn;
    uint32_t si;
    uint32_t sj;

    memset(&jb, 0, sizeof(jb));
    if (chrisc_compile(src, strlen(src), code, sizeof(code), &cr) == 0)
        return -1;
    image.version = CLVM_VERSION;
    image.flags = 0;
    image.entry = cr.entry;
    image.code_size = (uint32_t)cr.code_size;
    image.checksum = 0;
    image.code = code;
    if (jit_alloc(&jb) != 0)
        return -1;
    if (jit_compile_image(&image, &jb, &fn) != 0)
        return -1;

    clvm_vm_init(&vi, &image, test_sys, 0);
    clvm_vm_init(&vj, &image, test_sys, 0);
    sys_count = 0;
    run_interp(&vi, 64, &si);
    sys_count = 0;
    run_jit(&vj, fn, 64, &sj);
    assert(vi.state == vj.state);
    assert(sys_count > 0);
    jit_free(&jb);
    return 0;
}

static int test_cube(void) {
    FILE *f;
    char src[8192];
    size_t n;
    uint8_t code[8192];
    ChrisResult cr;
    ClvmImage image;
    JitBuf jb;
    JitFn fn;

    f = fopen("GAMES/CUBE.CC", "rb");
    if (!f)
        return 0;
    n = fread(src, 1, sizeof(src) - 1u, f);
    fclose(f);
    src[n] = 0;
    if (chrisc_compile(src, n, code, sizeof(code), &cr) == 0)
        return -1;
    image.version = CLVM_VERSION;
    image.flags = 0;
    image.entry = cr.entry;
    image.code_size = (uint32_t)cr.code_size;
    image.checksum = 0;
    image.code = code;
    memset(&jb, 0, sizeof(jb));
    if (jit_alloc(&jb) != 0)
        return -1;
    if (jit_compile_image(&image, &jb, &fn) != 0) {
        jit_free(&jb);
        return -1;
    }
    jit_free(&jb);
    return 0;
}

int main(void) {
    if (test_loop() != 0) {
        fputs("test_jit_vm: loop failed\n", stderr);
        return 1;
    }
    if (test_cube() != 0) {
        fputs("test_jit_vm: cube compile failed\n", stderr);
        return 1;
    }
    puts("test_jit_vm: ok");
    return 0;
}
