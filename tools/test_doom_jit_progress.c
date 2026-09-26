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
    (void)id; (void)user;
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
    uint32_t steps = 0;
    uint16_t max_sp = 0;
    uint32_t max_sp_pc = 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    memset(&vi, 0, sizeof(vi));
    clvm_vm_init(&vi, &img, nosys, 0);
    grow(&vi);
    while (steps < 500000u && vi.state != CLVM_FAULTED && vi.state != CLVM_HALTED) {
        ClvmStepResult r = clvm_step(&vi, 1);
        steps++;
        if (vi.sp > max_sp) {
            max_sp = vi.sp;
            max_sp_pc = vi.pc;
        }
        if (r == CLVM_STEP_FAULT) break;
        if (r == CLVM_STEP_YIELD) {
            vi.state = CLVM_READY;
            vi.wake_tick = 0;
        }
        if (steps % 50000u == 0)
            printf("interp step=%u pc=%u sp=%u max_sp=%u\n",
                   steps, vi.pc, vi.sp, max_sp);
    }
    printf("interp done steps=%u pc=%u sp=%u max_sp=%u@%u state=%d fault=%d\n",
           steps, vi.pc, vi.sp, max_sp, max_sp_pc, (int)vi.state, (int)vi.fault);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;
    memset(&vj, 0, sizeof(vj));
    clvm_vm_init(&vj, &img, nosys, 0);
    grow(&vj);
    {
        uint32_t i;
        for (i = 0; i < 50; i++) {
            ClvmStepResult r = fn(&vj, 50000u, i);
            printf("jit i=%u r=%d pc=%u sp=%u state=%d\n",
                   i, (int)r, vj.pc, vj.sp, (int)vj.state);
            if (r == CLVM_STEP_FAULT || r == CLVM_STEP_HALT)
                break;
            if (vj.state == CLVM_WAITING)
                vj.state = CLVM_READY;
        }
    }
    return 0;
}
