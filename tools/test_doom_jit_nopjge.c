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

int main(int argc, char **argv) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    ClvmVm vm;
    uint32_t *table;
    uint32_t off;
    int mode = argc > 1 ? atoi(argv[1]) : 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];

    /* entry PUSH encoding:
     * +0  mov eax,imm32 (5)
     * +5  movsxd (3)
     * +8  movzx ecx,[sp] (7)
     * +15 cmp ecx,imm32 (6)
     * +21 jge fault (6)   <-- nop this in mode 1
     * +27 store/inc/sp (remaining)
     */
    printf("bytes+21: %02x %02x %02x %02x %02x %02x\n",
           buf.w[off+21], buf.w[off+22], buf.w[off+23],
           buf.w[off+24], buf.w[off+25], buf.w[off+26]);

    if (mode == 1) {
        memset(buf.w + off + 21, 0x90, 6); /* nop jge */
        printf("nopped jge\n");
    } else if (mode == 2) {
        /* Replace whole first push with just set sp=1, stack0=101, return slice */
        static const uint8_t stub[] = {
            0x48, 0xC7, 0x83, 0x10, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, /* mov qword [rbx+0x10], 101 */
            0x66, 0xC7, 0x83, 0x10, 0x08, 0x00, 0x00, 0x01, 0x00, /* mov word [rbx+0x810], 1 */
            0x31, 0xC0, 0x5B, 0xC9, 0xC3
        };
        memcpy(buf.w + off, stub, sizeof(stub));
        printf("stubbed push\n");
    }

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    {
        int r = (int)fn(&vm, 100, 0);
        printf("mode=%d r=%d sp=%u state=%d stack0=%lld pc=%u\n",
               mode, r, vm.sp, (int)vm.state, (long long)vm.stack[0], vm.pc);
    }
    return 0;
}
