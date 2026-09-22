#include <stdio.h>
#include <stddef.h>
#include "clvm_vm.h"

int main(void) {
    printf("pc=%zu stack=%zu sp=%zu mem=%zu msz=%zu\n",
           offsetof(ClvmVm, pc), offsetof(ClvmVm, stack), offsetof(ClvmVm, sp),
           offsetof(ClvmVm, memory), offsetof(ClvmVm, mem_size));
    printf("calls=%zu csp=%zu state=%zu onsp=%zu sizeof=%zu\n",
           offsetof(ClvmVm, calls), offsetof(ClvmVm, csp),
           offsetof(ClvmVm, state), offsetof(ClvmVm, on_safepoint),
           sizeof(ClvmVm));
    return 0;
}
