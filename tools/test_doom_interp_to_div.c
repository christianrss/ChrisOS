#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

void serial_puts(const char *s) { (void)s; }
void serial_write_u64(uint64_t v) { (void)v; }

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)id; (void)user;
    /* liberal success for any sys during init */
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    ClvmVm vm;
    uint32_t steps = 0;
    const uint32_t target = 820901;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    /* grow RAM like doom */
    {
        uint8_t *big = calloc(32u * 1024u * 1024u, 1);
        clvm_vm_set_memory(&vm, big, 32u * 1024u * 1024u);
    }

    while (vm.pc != target && vm.state != CLVM_FAULTED &&
           vm.state != CLVM_HALTED && steps < 50000000u) {
        ClvmStepResult r = clvm_step(&vm, 1);
        steps++;
        if (r == CLVM_STEP_FAULT) break;
        if (r == CLVM_STEP_YIELD) {
            vm.state = CLVM_READY;
            vm.wake_tick = 0;
        }
    }
    printf("interp after %u steps: pc=%u sp=%u state=%d fault=%d\n",
           steps, vm.pc, vm.sp, (int)vm.state, (int)vm.fault);
    if (vm.sp >= 1)
        printf("  stack top=%lld\n", (long long)vm.stack[vm.sp - 1]);
    if (vm.sp >= 2)
        printf("  stack 2nd=%lld\n", (long long)vm.stack[vm.sp - 2]);

    if (vm.pc == target && vm.sp >= 2) {
        ClvmStepResult r = clvm_step(&vm, 1);
        printf("DIV step r=%d pc=%u sp=%u fault=%d top=%lld\n",
               (int)r, vm.pc, vm.sp, (int)vm.fault,
               vm.sp ? (long long)vm.stack[vm.sp - 1] : -1);
    }
    return 0;
}
