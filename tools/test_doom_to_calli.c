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
    (void)user;
    if (id == 56) { /* malloc */
        int32_t sz;
        uint64_t p;
        if (!clvm_vm_pop(vm, &sz))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)(uint32_t)sz, &p))
            return clvm_vm_push(vm, 0) ? 0 : -1;
        return clvm_vm_push(vm, (int32_t)p) ? 0 : -1;
    }
    if (id == 53) { /* fwrite */
        int32_t fd, addr, n;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) || !clvm_vm_pop(vm, &fd))
            return -1;
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (id == 50) { /* fopen-ish — push fake fd 3 */
        int32_t a, b;
        if (!clvm_vm_pop(vm, &b) || !clvm_vm_pop(vm, &a))
            return -1;
        return clvm_vm_push(vm, 3) ? 0 : -1;
    }
    /* default: push 0 success */
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
    ClvmVm vm;
    const uint32_t want = 129338; /* CALLI */
    uint32_t steps = 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    grow(&vm);

    while (vm.pc != want && steps < 2000000u &&
           vm.state != CLVM_FAULTED && vm.state != CLVM_HALTED) {
        ClvmStepResult r = clvm_step(&vm, 1);
        steps++;
        if (r == CLVM_STEP_FAULT)
            break;
        if (r == CLVM_STEP_YIELD) {
            vm.state = CLVM_READY;
            vm.wake_tick = 0;
        }
    }
    printf("steps=%u pc=%u sp=%u state=%d fault=%d\n",
           steps, vm.pc, vm.sp, (int)vm.state, (int)vm.fault);
    if (vm.sp > 0)
        printf("top=%lld\n", (long long)vm.stack[vm.sp - 1]);
    if (vm.pc == want) {
        ClvmStepResult r = clvm_step(&vm, 1); /* CALLI */
        printf("after CALLI r=%d pc=%u sp=%u csp=%u\n",
               (int)r, vm.pc, vm.sp, vm.csp);
    }
    return 0;
}
