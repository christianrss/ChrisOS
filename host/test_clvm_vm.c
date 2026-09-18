#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../compiler/clvm/clvm_vm.h"

static int sys(ClvmVm *vm, int32_t id, void *user) {
    int32_t delay;
    uint32_t now = *(uint32_t *)user;
    if (id != 1 || !clvm_vm_pop(vm, &delay) || delay < 0) return -1;
    clvm_vm_wait(vm, now + (uint32_t)delay);
    return 0;
}

static uint32_t mem0(const ClvmVm *vm) {
    return (uint32_t)vm->memory[0] |
           ((uint32_t)vm->memory[1] << 8) |
           ((uint32_t)vm->memory[2] << 16) |
           ((uint32_t)vm->memory[3] << 24);
}

int main(void) {
    static const uint8_t code[] = {
        CL_OP_PUSH,0,0,0,0, CL_OP_LOAD,
        CL_OP_PUSH,1,0,0,0, CL_OP_ADD,
        CL_OP_PUSH,0,0,0,0, CL_OP_STORE,
        CL_OP_PUSH,2,0,0,0, CL_OP_PUSH,1,0,0,0, CL_OP_SYS,
        CL_OP_JMP,0xe0,0xff
    };
    ClvmImage image = {1, 1, 0, sizeof(code), 0, code};
    ClvmVm vm;
    uint32_t now = 10;
    clvm_vm_init(&vm, &image, sys, &now);
    assert(clvm_step(&vm, 100) == CLVM_STEP_YIELD);
    assert(mem0(&vm) == 1);
    assert(vm.state == CLVM_WAITING);
    clvm_vm_wake(&vm, 11);
    assert(vm.state == CLVM_WAITING);
    clvm_vm_wake(&vm, 12);
    assert(clvm_step(&vm, 100) == CLVM_STEP_YIELD);
    assert(mem0(&vm) == 2);

    clvm_vm_init(&vm, &image, sys, &now);
    assert(clvm_step(&vm, 3) == CLVM_STEP_SLICE);
    assert(vm.executed == 3);
    puts("test_clvm_vm: ok");
    return 0;
}
