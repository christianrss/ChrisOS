#include <stdio.h>
#include <stddef.h>
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
    (void)vm; (void)id; (void)user;
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
    ClvmStepResult r;

    printf("off pc=%zu sp=%zu stack=%zu state=%zu csp=%zu mem=%zu\n",
           offsetof(ClvmVm, pc), offsetof(ClvmVm, sp),
           offsetof(ClvmVm, stack), offsetof(ClvmVm, state),
           offsetof(ClvmVm, csp), offsetof(ClvmVm, memory));

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    printf("before: pc=%u sp=%u state=%d fault=%d\n",
           vm.pc, vm.sp, (int)vm.state, (int)vm.fault);

    r = fn(&vm, 1u, 0);  /* budget 1 — but native may ignore until backedge */
    printf("after1: r=%d pc=%u sp=%u state=%d fault=%d executed=%llu\n",
           (int)r, vm.pc, vm.sp, (int)vm.state, (int)vm.fault,
           (unsigned long long)vm.executed);

    /* Try interpreter one step for comparison */
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    r = clvm_step(&vm, 1);
    printf("interp1: r=%d pc=%u sp=%u state=%d fault=%d stack0=%lld\n",
           (int)r, vm.pc, vm.sp, (int)vm.state, (int)vm.fault,
           (long long)vm.stack[0]);
    return 0;
}
