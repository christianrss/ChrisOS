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
void serial_write_hex(uint64_t v) { printf("%llx", (unsigned long long)v); }

static FILE *g_wad;
static int g_opened_wad;

static const char *guest_string(ClvmVm *vm, int32_t addr) {
    if (addr < 0 || (uint64_t)(uint32_t)addr >= vm->mem_size)
        return NULL;
    return (const char *)vm->memory + (uint32_t)addr;
}

static int sys_stub(ClvmVm *vm, int32_t id, void *user) {
    (void)user;
    if (id == 50) {
        int32_t addr;
        const char *path;
        if (!clvm_vm_pop(vm, &addr))
            return -1;
        path = guest_string(vm, addr);
        if (!path)
            return -1;
        g_wad = fopen(path, "rb");
        g_opened_wad = g_wad != NULL;
        return clvm_vm_push(vm, g_wad ? 3 : -1) ? 0 : -1;
    }
    if (id == 51) {
        int32_t fd;
        if (!clvm_vm_pop(vm, &fd))
            return -1;
        if (g_wad) {
            fclose(g_wad);
            g_wad = NULL;
        }
        return clvm_vm_push(vm, 0) ? 0 : -1;
    }
    if (id == 52) {
        int32_t fd, addr, n;
        size_t got;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) ||
            !clvm_vm_pop(vm, &fd) || !g_wad || n < 0 || addr < 0 ||
            (uint64_t)(uint32_t)addr + (uint32_t)n > vm->mem_size)
            return -1;
        got = fread(vm->memory + (uint32_t)addr, 1, (size_t)n, g_wad);
        return clvm_vm_push(vm, (int32_t)got) ? 0 : -1;
    }
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
    if (id == 54) {
        int32_t addr;
        const char *path;
        FILE *f;
        long size;
        if (!clvm_vm_pop(vm, &addr))
            return -1;
        path = guest_string(vm, addr);
        f = path ? fopen(path, "rb") : NULL;
        if (!f)
            return clvm_vm_push(vm, -1) ? 0 : -1;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
        return clvm_vm_push(vm, (int32_t)size) ? 0 : -1;
    }
    if (id == 55) {
        int32_t addr;
        const char *path;
        FILE *f;
        if (!clvm_vm_pop(vm, &addr))
            return -1;
        path = guest_string(vm, addr);
        f = path ? fopen(path, "rb") : NULL;
        if (f)
            fclose(f);
        return clvm_vm_push(vm, f ? 1 : 0) ? 0 : -1;
    }
    if (id == 56) { /* malloc */
        int64_t sz;
        uint64_t p;
        if (!clvm_vm_pop64(vm, &sz))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)sz, &p))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, (int64_t)p) ? 0 : -1;
    }
    if (id == 57) {
        int64_t ptr;
        if (!clvm_vm_pop64(vm, &ptr))
            return -1;
        return 0;
    }
    if (id == 65) {
        int32_t fd, offset;
        if (!clvm_vm_pop(vm, &offset) || !clvm_vm_pop(vm, &fd) || !g_wad)
            return -1;
        return clvm_vm_push(vm, fseek(g_wad, offset, SEEK_SET) == 0 ? 1 : 0)
                   ? 0 : -1;
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
        printf("step %u -> %d state=%d fault=%d pc=%u fpc=%u sp=%u csp=%u\n",
               i, (int)r, (int)vm.state, (int)vm.fault, vm.pc, vm.fault_pc,
               vm.sp, vm.csp);
        if (r == CLVM_STEP_FAULT && vm.sp > 0) {
            printf("fault stack top=%lld\n",
                   (long long)vm.stack[vm.sp - 1u]);
        }
        if (r == CLVM_STEP_FAULT || r == CLVM_STEP_HALT)
            break;
        if (g_opened_wad) {
            printf("doom JIT reached WAD I/O\n");
            return 0;
        }
    }
    return (vm.state == CLVM_FAULTED || !g_opened_wad) ? 10 : 0;
}
