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
    (void)id;
    (void)user;
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

static void run_one(int use_jit) {
    /* 270540=0x000420CC, 271268=0x000423A4 */
    uint8_t code[] = {
        0x01, 0xCC, 0x20, 0x04, 0x00, /* PUSH 270540 */
        0x26,                         /* LOAD64 */
        0x01, 0xA4, 0x23, 0x04, 0x00, /* PUSH 271268 */
        0x27,                         /* STORE64 */
        0x08                          /* HALT */
    };
    ClvmImage img;
    JitBuf buf;
    JitFn fn = 0;
    ClvmVm vm;
    uint8_t *mem;
    uint64_t v540, v268;

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
    }
    if (use_jit) {
        memset(&buf, 0, sizeof(buf));
        if (jit_compile_image(&img, &buf, &fn) != 0) {
            printf("jit compile fail\n");
            return;
        }
        fn(&vm, 1000u, 0);
    } else {
        while (vm.state != CLVM_HALTED && vm.state != CLVM_FAULTED)
            clvm_step(&vm, 1);
    }
    memcpy(&v540, mem + 270540, 8);
    memcpy(&v268, mem + 271268, 8);
    printf("%s: g540=%llu g268=%llu %s\n", use_jit ? "jit" : "interp",
           (unsigned long long)v540, (unsigned long long)v268,
           v268 == 65544ull ? "OK" : "FAIL");
}

int main(void) {
    run_one(0);
    run_one(1);
    return 0;
}
