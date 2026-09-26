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

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];

    printf("fn=%p entry_native=%p\n", (void *)(uintptr_t)fn, (void *)(buf.x + off));
    printf("first 16: ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf.x[off+i]);
    printf("\n");

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    printf("vm=%p sp=%u\n", (void *)&vm, vm.sp);

    /* Put int3 at entry to see if we reach it — will abort */
    /* Instead: replace entry with sequence that increments a global via side effect */
    {
        /* mov eax, 0x12345678; pop rbx; leave; ret */
        static const uint8_t stub[] = {
            0xB8, 0x78, 0x56, 0x34, 0x12,
            0x5B, 0xC9, 0xC3
        };
        memcpy(buf.w + off, stub, sizeof(stub));
        __builtin___clear_cache((char *)buf.x + off, (char *)buf.x + off + 16);
    }
    {
        int r = (int)fn(&vm, 100, 0);
        printf("stub r=0x%x state=%d\n", r, (int)vm.state);
    }

    /* Now restore isn't needed — recompile fresh and run unpatched with watch */
    memset(&buf, 0, sizeof(buf));
    /* leak old mapping ok for test */
    jit_alloc(&buf);
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);

    /* Patch ONLY after first complete PUSH (at second mov eax): insert
     * mov eax, 0x111; pop rbx; leave; ret to see if we get past first push */
    {
        /* first push is 5+3+7+6+6+8+2+7 = 44 bytes? let's count:
         * mov eax imm = 5
         * movsxd = 3
         * movzx = 7
         * cmp = 6
         * jge = 6
         * store = 8
         * inc = 2
         * store sp = 7
         * total = 44
         */
        uint32_t after_push = off + 44;
        static const uint8_t stub[] = {
            0xB8, 0x11, 0x11, 0x00, 0x00,
            0x5B, 0xC9, 0xC3
        };
        printf("byte at after_push: %02x (expect b8 for next PUSH)\n",
               buf.w[after_push]);
        memcpy(buf.w + after_push, stub, sizeof(stub));
        __builtin___clear_cache((char *)buf.x + after_push,
                                (char *)buf.x + after_push + 16);
        {
            int r = (int)fn(&vm, 100, 0);
            printf("after-first-push stub r=0x%x sp=%u stack0=%lld state=%d\n",
                   r, vm.sp, (long long)vm.stack[0], (int)vm.state);
        }
    }
    return 0;
}
