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
    ClvmStepResult r;
    uint32_t *table;
    uint32_t off;
    uint32_t table_off;
    uint64_t tab_addr, blob_addr;
    uint32_t tab_imm_off = 0, blob_imm_off = 0;
    uint32_t i;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;

    table_off = buf.used - img.code_size * 4u;
    table = (uint32_t *)(void *)(buf.w + table_off);
    off = table[img.entry];
    tab_addr = (uint64_t)(uintptr_t)buf.x + table_off;
    blob_addr = (uint64_t)(uintptr_t)buf.x;

    /* find movabs immediates in dispatch: scan for 48 b9 / 48 ba near start */
    for (i = 0; i + 10 < 200; i++) {
        if (buf.w[i] == 0x48 && buf.w[i + 1] == 0xB9 && tab_imm_off == 0)
            tab_imm_off = i + 2;
        if (buf.w[i] == 0x48 && buf.w[i + 1] == 0xBA && blob_imm_off == 0)
            blob_imm_off = i + 2;
    }
    {
        uint64_t patched_tab = 0, patched_blob = 0;
        for (i = 0; i < 8; i++) {
            patched_tab |= (uint64_t)buf.w[tab_imm_off + i] << (8 * i);
            patched_blob |= (uint64_t)buf.w[blob_imm_off + i] << (8 * i);
        }
        printf("tab expect=%llx patched=%llx %s\n",
               (unsigned long long)tab_addr, (unsigned long long)patched_tab,
               tab_addr == patched_tab ? "OK" : "BAD");
        printf("blob expect=%llx patched=%llx %s\n",
               (unsigned long long)blob_addr, (unsigned long long)patched_blob,
               blob_addr == patched_blob ? "OK" : "BAD");
        printf("table[entry]=%u read_via_patched=%u\n",
               off, *(uint32_t *)(uintptr_t)(patched_tab + (uint64_t)img.entry * 4u));
    }

    /* Patch entry native code to: xor eax,eax; pop rbx; leave; ret  (return SLICE) */
    {
        static const uint8_t ret_slice[] = {
            0x31, 0xC0,       /* xor eax, eax */
            0x5B,             /* pop rbx */
            0xC9, 0xC3        /* leave; ret */
        };
        memcpy(buf.w + off, ret_slice, sizeof(ret_slice));
        /* need WX - already have from seal */
    }

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, nosys, 0);
    r = fn(&vm, 100, 0);
    printf("patched-entry r=%d state=%d pc=%u (SLICE=0 FAULT=3)\n",
           (int)r, (int)vm.state, vm.pc);
    return 0;
}
