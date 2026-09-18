#include "lang_sys.h"

#include "../compiler/clvm/clvm_vm.h"
#include "pit.h"

int kernel_lang_sys(ClvmVm *vm, int32_t id, void *user) {
    int32_t value;
    (void)user;
    if (id == 11) {
        return clvm_vm_push(vm, (int32_t)ticks) ? 0 : -1;
    }
    if (id == 12) {
        if (!clvm_vm_pop(vm, &value) || value < 0) {
            return -1;
        }
        clvm_vm_wait(vm, (uint32_t)ticks + (uint32_t)value);
        return 0;
    }
    return -1;
}

