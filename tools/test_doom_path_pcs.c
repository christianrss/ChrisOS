#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)id; (void)user;
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    ClvmVm vm;
    uint32_t steps = 0;
    uint32_t checkpoints[] = {1, 10, 100, 1000, 5000, 10000, 30000, 50000, 80000, 100000, 107000};
    uint32_t ci = 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    {
        uint8_t *big = calloc(32u * 1024u * 1024u, 1);
        clvm_vm_set_memory(&vm, big, 32u * 1024u * 1024u);
    }

    while (steps < 107377u && vm.state != CLVM_FAULTED) {
        if (ci < sizeof(checkpoints)/sizeof(checkpoints[0]) &&
            steps == checkpoints[ci]) {
            printf("step %u pc=%u sp=%u\n", steps, vm.pc, vm.sp);
            ci++;
        }
        {
            ClvmStepResult r = clvm_step(&vm, 1);
            if (r == CLVM_STEP_FAULT) break;
            if (r == CLVM_STEP_YIELD) {
                vm.state = CLVM_READY;
                vm.wake_tick = 0;
            }
        }
        steps++;
    }
    printf("final step %u pc=%u sp=%u\n", steps, vm.pc, vm.sp);
    return 0;
}
