#include <stdio.h>
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
    uint32_t *table;
    uint32_t off;
    /* movzx eax, word [rbx+0x810]; pop rbx; leave; ret */
    static const uint8_t read_sp[] = {
        0x0F, 0xB7, 0x83, 0x10, 0x08, 0x00, 0x00,
        0x5B, 0xC9, 0xC3
    };

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];
    memcpy(buf.w + off, read_sp, sizeof(read_sp));

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    vm.sp = 42; /* distinctive */
    printf("C view sp=%u at %p off810=%u\n", vm.sp, (void *)&vm,
           *(uint16_t *)((uint8_t *)&vm + 0x810));
    {
        int r = (int)fn(&vm, 100, 0);
        printf("native returned eax=%d state=%d\n", r, (int)vm.state);
    }
    return 0;
}
