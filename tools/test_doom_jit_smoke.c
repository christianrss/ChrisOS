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

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    ClvmVm vm;
    uint32_t i;
    uint32_t slices = 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK)
        return 1;
    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    {
        uint8_t *big = calloc(32u * 1024u * 1024u, 1);
        clvm_vm_set_memory(&vm, big, 32u * 1024u * 1024u);
    }
    for (i = 0; i < 2000; i++) {
        ClvmStepResult r = fn(&vm, 100000u, i);
        if (r == CLVM_STEP_SLICE || r == CLVM_STEP_YIELD) {
            slices++;
            if (vm.state == CLVM_WAITING)
                vm.state = CLVM_READY;
            continue;
        }
        printf("stop i=%u r=%d pc=%u sp=%u state=%d fault=%d\n",
               i, (int)r, vm.pc, vm.sp, (int)vm.state, (int)vm.fault);
        return (r == CLVM_STEP_HALT) ? 0 : 10;
    }
    printf("ok slices=%u pc=%u sp=%u state=%d\n",
           slices, vm.pc, vm.sp, (int)vm.state);
    return 0;
}
