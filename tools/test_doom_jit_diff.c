#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)id;
    (void)user;
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

static void grow(ClvmVm *vm) {
    uint8_t *big = calloc(32u * 1024u * 1024u, 1);
    clvm_vm_set_memory(vm, big, 32u * 1024u * 1024u);
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    ClvmVm vi, vj;
    uint32_t steps;

    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz)
        return 1;
    fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK)
        return 1;
    printf("entry=%u code=%u\n", img.entry, img.code_size);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;

    memset(&vi, 0, sizeof(vi));
    clvm_vm_init(&vi, &img, nosys, 0);
    grow(&vi);
    memset(&vj, 0, sizeof(vj));
    clvm_vm_init(&vj, &img, nosys, 0);
    grow(&vj);

    for (steps = 0; steps < 5000000u; steps++) {
        ClvmStepResult ri, rj;
        uint32_t pci = vi.pc, pcj = vj.pc;
        uint16_t spi = vi.sp, spj = vj.sp;
        uint8_t op = (pci < img.code_size) ? img.code[pci] : 0xff;

        ri = clvm_step(&vi, 1);
        if (ri == CLVM_STEP_YIELD) {
            vi.state = CLVM_READY;
            vi.wake_tick = 0;
        }
        rj = fn(&vj, 1, 0);
        if (rj == CLVM_STEP_YIELD) {
            vj.state = CLVM_READY;
            vj.wake_tick = 0;
        }
        if (vi.pc != vj.pc || vi.sp != vj.sp ||
            (vi.sp > 0 && vj.sp > 0 &&
             vi.stack[vi.sp - 1] != vj.stack[vj.sp - 1]) ||
            ri != rj) {
            printf("DIVERGE step=%u op=0x%02x\n", steps, op);
            printf("  before: pci=%u spi=%u pcj=%u spj=%u\n",
                   pci, spi, pcj, spj);
            printf("  after:  pi=%u si=%u ri=%d fi=%d\n",
                   vi.pc, vi.sp, (int)ri, (int)vi.fault);
            printf("          pj=%u sj=%u rj=%d fj=%d st=%d\n",
                   vj.pc, vj.sp, (int)rj, (int)vj.fault, (int)vj.state);
            if (vi.sp > 0)
                printf("  topi=%lld\n", (long long)vi.stack[vi.sp - 1]);
            if (vj.sp > 0)
                printf("  topj=%lld\n", (long long)vj.stack[vj.sp - 1]);
            return 10;
        }
        if (ri == CLVM_STEP_FAULT || ri == CLVM_STEP_HALT) {
            printf("both stop step=%u r=%d pc=%u sp=%u fault=%d\n",
                   steps, (int)ri, vi.pc, vi.sp, (int)vi.fault);
            return 0;
        }
        if ((steps & 0xffffu) == 0u)
            printf("ok step=%u pc=%u sp=%u\n", steps, vi.pc, vi.sp);
    }
    printf("ran out steps pc=%u sp=%u\n", vi.pc, vi.sp);
    return 0;
}
