/* Host repro: JIT-compile GAMES/DOOM/ENGINE.CLV and step once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

/* stubs for freestanding symbols pulled by jit_compile.c */
void serial_puts(const char *s) { fputs(s, stdout); }
void serial_write_u64(uint64_t v) { printf("%llu", (unsigned long long)v); }

static int sys_stub(ClvmVm *vm, int32_t id, void *user) {
    (void)user;
    if (id == 53) { /* fwrite -> treat as ok / print */
        int32_t fd, addr, n;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) || !clvm_vm_pop(vm, &fd))
            return -1;
        if (fd == 1 || fd == 2) {
            if (addr >= 0 && n > 0 && (uint32_t)addr + (uint32_t)n <= vm->mem_size) {
                fwrite(vm->memory + (uint32_t)addr, 1, (size_t)n, stdout);
                fflush(stdout);
            }
        }
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (id == 56) { /* malloc */
        int32_t sz;
        uint64_t p;
        if (!clvm_vm_pop(vm, &sz))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)(uint32_t)sz, &p))
            return clvm_vm_push(vm, 0) ? 0 : -1;
        return clvm_vm_push(vm, (int32_t)p) ? 0 : -1;
    }
    /* default: pop nothing specific — many syscalls; just succeed with 0 */
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f;
    uint8_t *file;
    long sz;
    ClvmImage img;
    ClvmLoadError err;
    JitBuf buf;
    JitFn fn;
    ClvmVm vm;
    ClvmStepResult r;
    uint32_t *table;
    uint32_t i;
    uint32_t nat_at_entry;

    f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    if (!f) {
        perror("ENGINE.CLV");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = (uint8_t *)malloc((size_t)sz);
    if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "read fail\n");
        return 1;
    }
    fclose(f);

    err = clvm_parse(file, (size_t)sz, &img);
    if (err != CL_LOAD_OK) {
        fprintf(stderr, "parse %s\n", clvm_load_error(err));
        return 1;
    }
    printf("code_size=%u entry=%u mem_hint=%u\n",
           img.code_size, img.entry, img.mem_hint);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0) {
        fprintf(stderr, "jit_compile_image failed\n");
        return 2;
    }
    printf("jit ok used=%u cap=%u fn=%p\n", buf.used, buf.cap, (void *)(uintptr_t)fn);

    /* Find jump table: it's the last code_size uint32s before end of used...
     * Actually table starts at used - code_size*4 after compile... stored in buf.
     * We need g_nat[entry] — re-walk: table is at end. */
    if (buf.used < img.code_size * 4u) {
        fprintf(stderr, "buffer too small for table\n");
        return 3;
    }
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    nat_at_entry = table[img.entry];
    printf("table[entry]=%u (0 means FAULT path)\n", nat_at_entry);
    if (nat_at_entry == 0) {
        fprintf(stderr, "BUG: entry has no native offset\n");
        /* count how many insn starts have non-zero */
        {
            uint32_t nz = 0;
            for (i = 0; i < img.code_size; i++)
                if (table[i])
                    nz++;
            printf("nonzero table entries: %u / %u\n", nz, img.code_size);
        }
        return 4;
    }

    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, sys_stub, 0);
    /* grow guest heap like doom */
    {
        uint8_t *big = (uint8_t *)calloc(32u * 1024u * 1024u, 1);
        if (!big)
            return 5;
        clvm_vm_set_memory(&vm, big, 32u * 1024u * 1024u);
    }

    printf("running jit steps, pc=%u sp=%u\n", vm.pc, vm.sp);
    for (i = 0; i < 20; i++) {
        r = fn(&vm, 50000u, i);
        printf("step %u -> %d state=%d fault=%d pc=%u sp=%u\n",
               i, (int)r, (int)vm.state, (int)vm.fault, vm.pc, vm.sp);
        if (r == CLVM_STEP_FAULT || r == CLVM_STEP_HALT)
            break;
    }
    return (vm.state == CLVM_FAULTED) ? 10 : 0;
}
