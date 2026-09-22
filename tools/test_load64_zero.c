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

int main(void) {
    uint8_t code[] = {
        0x01, 0xCC, 0x20, 0x04, 0x00, /* PUSH 270540 */
        0x26,                         /* LOAD64 */
        0x08                          /* HALT */
    };
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    ClvmVm vm;
    uint8_t *mem;

    memset(&img, 0, sizeof(img));
    img.code = code;
    img.code_size = sizeof(code);
    img.entry = 0;
    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 1;
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    mem = calloc(32u * 1024u * 1024u, 1);
    clvm_vm_set_memory(&vm, mem, 32u * 1024u * 1024u);
    fn(&vm, 100u, 0);
    printf("sp=%u top=%lld state=%d\n", vm.sp,
           vm.sp ? (long long)vm.stack[vm.sp - 1] : -1LL, (int)vm.state);
    return vm.sp == 1 && vm.stack[0] == 0 ? 0 : 10;
}
