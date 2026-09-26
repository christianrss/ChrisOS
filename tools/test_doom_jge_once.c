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
    uint32_t off, j, fault_off = 20;
    int32_t rel;
    uint32_t tgt;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    jit_compile_image(&img, &buf, &fn);
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    off = table[img.entry];

    for (j = 0; j < 40; j++) {
        if (buf.w[off+j]==0x0F && buf.w[off+j+1]==0x8D) {
            printf("jge bytes: %02x %02x %02x %02x %02x %02x\n",
                   buf.w[off+j], buf.w[off+j+1], buf.w[off+j+2],
                   buf.w[off+j+3], buf.w[off+j+4], buf.w[off+j+5]);
            rel = (int32_t)((uint32_t)buf.w[off+j+2] |
                            ((uint32_t)buf.w[off+j+3] << 8) |
                            ((uint32_t)buf.w[off+j+4] << 16) |
                            ((uint32_t)buf.w[off+j+5] << 24));
            tgt = off + j + 6 + (uint32_t)rel;
            printf("rel=%d tgt=%u fault=%u %s\n", (int)rel, tgt, fault_off,
                   tgt == fault_off ? "OK" : "BAD");
            break;
        }
    }

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    printf("step r=%d sp=%u state=%d\n",
           (int)fn(&vm, 100, 0), vm.sp, (int)vm.state);
    return 0;
}
