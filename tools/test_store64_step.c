#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)id;
    (void)user;
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    uint8_t code[] = {
        0x01, 0x0C, 0x1E, 0x04, 0x00,
        0x26,
        0x01, 0xA4, 0x23, 0x04, 0x00,
        0x27,
        0x08
    };
    ClvmImage img;
    ClvmVm vm;
    uint8_t *mem;
    int i;

    memset(&img, 0, sizeof(img));
    img.code = code;
    img.code_size = (uint32_t)sizeof(code);
    img.entry = 0;
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    mem = calloc(32u * 1024u * 1024u, 1);
    clvm_vm_set_memory(&vm, mem, 32u * 1024u * 1024u);
    {
        uint64_t p = 65544ull;
        memcpy(mem + 270540, &p, 8);
        printf("seeded mem[270540]=%llu mem_size=%llu\n",
               (unsigned long long)p, (unsigned long long)vm.mem_size);
    }
    for (i = 0; i < 10; i++) {
        uint32_t pc0 = vm.pc;
        uint8_t op = (pc0 < sizeof(code)) ? code[pc0] : 0xff;
        ClvmStepResult r = clvm_step(&vm, 1);
        printf("i=%d op=0x%02x pc=%u->%u sp=%u r=%d fault=%d",
               i, op, pc0, vm.pc, vm.sp, (int)r, (int)vm.fault);
        if (vm.sp >= 1)
            printf(" top=%lld", (long long)vm.stack[vm.sp - 1]);
        if (vm.sp >= 2)
            printf(" 2nd=%lld", (long long)vm.stack[vm.sp - 2]);
        printf("\n");
        if (r == CLVM_STEP_FAULT || r == CLVM_STEP_HALT)
            break;
    }
    {
        uint64_t v540 = 0, v268 = 0;
        memcpy(&v540, mem + 270540, 8);
        memcpy(&v268, mem + 271268, 8);
        printf("final g540=%llu g268=%llu\n",
               (unsigned long long)v540, (unsigned long long)v268);
    }
    return 0;
}
