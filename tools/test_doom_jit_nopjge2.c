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
    uint32_t i;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];

    /* nop the jge */
    memset(buf.w + off + 21, 0x90, 6);
    __builtin___clear_cache((char *)buf.x + off, (char *)buf.x + off + 64);

    printf("after nop+clear: ");
    for (i = 21; i < 27; i++)
        printf("%02x ", buf.w[off + i]);
    printf("\n");

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    {
        int r = (int)fn(&vm, 5, 0);
        printf("r=%d sp=%u state=%d stack0=%lld executed=%llu\n",
               r, vm.sp, (int)vm.state, (long long)vm.stack[0],
               (unsigned long long)vm.executed);
    }

    /* Find which PC has g_nat == 24596 */
    printf("scanning table for nat~24596:\n");
    for (i = 0; i < img.code_size; i++) {
        if (table[i] >= 24500 && table[i] <= 24700)
            printf("  pc=%u nat=%u\n", i, table[i]);
    }
    return 0;
}
