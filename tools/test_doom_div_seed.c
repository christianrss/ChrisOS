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
    uint32_t *table;
    uint32_t off;
    uint32_t i;
    ClvmVm vm;
    const uint32_t div_pc = 820901;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[div_pc];
    printf("DIV nat off=%u bytes:\n", off);
    for (i = 0; i < 96; i++)
        printf("%02x%s", buf.w[off + i], ((i + 1) % 16) ? " " : "\n");
    printf("\n");

    /* Set PC to div, seed stack, jump via dispatch by calling fn */
    /* Stop after DIV: patch next insn to return SLICE. */
    {
        uint32_t next = table[div_pc + 1u];
        static const uint8_t slice[] = { 0x31, 0xC0, 0x5B, 0xC9, 0xC3 };
        memcpy(buf.w + next, slice, sizeof(slice));
        __builtin___clear_cache((char *)buf.x + next, (char *)buf.x + next + 8);
    }

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    vm.pc = div_pc;
    vm.stack[0] = 8192;
    vm.stack[1] = 4;
    vm.sp = 2;

    {
        int r = (int)fn(&vm, 100, 0);
        printf("seeded DIV: r=%d sp=%u state=%d fault=%d top=%lld pc=%u\n",
               r, vm.sp, (int)vm.state, (int)vm.fault,
               vm.sp ? (long long)vm.stack[vm.sp - 1] : -1, vm.pc);
    }
    return 0;
}
