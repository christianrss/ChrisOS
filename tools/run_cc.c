#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clvm.h"
#include "clvm_vm.h"

static FILE *g_files[16];

static int guest_str(ClvmVm *vm, int64_t addr, char *out, int cap) {
    int i = 0;
    if (addr < 0 || !vm->memory)
        return 0;
    while (i + 1 < cap && (uint64_t)addr + (uint64_t)i < vm->mem_size) {
        char c = (char)vm->memory[(uint64_t)addr + (uint64_t)i];
        out[i++] = c;
        if (c == 0)
            return 1;
    }
    out[i] = 0;
    return 0;
}

static int sys(ClvmVm *vm, int32_t id, void *user) {
    int64_t a, b, c;
    (void)user;
    if (id == 56) {
        uint64_t p = 0;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)a, &p))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, (int64_t)p) ? 0 : -1;
    }
    if (id == 50) {
        char path[128];
        int fd;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (!guest_str(vm, a, path, 128))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        for (fd = 3; fd < 16; ++fd) {
            if (!g_files[fd])
                break;
        }
        if (fd >= 16)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (strstr(path, "OUT"))
            g_files[fd] = fopen(path, "wb");
        else
            g_files[fd] = fopen(path, "rb");
        if (!g_files[fd])
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, fd) ? 0 : -1;
    }
    if (id == 51) {
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (a >= 3 && a < 16 && g_files[a]) {
            fclose(g_files[a]);
            g_files[a] = 0;
        }
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    if (id == 52 || id == 53) {
        int n;
        if (!clvm_vm_pop64(vm, &c) || !clvm_vm_pop64(vm, &b) || !clvm_vm_pop64(vm, &a))
            return -1;
        if (a < 3 || a >= 16 || !g_files[a] || b < 0 || c < 0)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if ((uint64_t)b + (uint64_t)c > vm->mem_size)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (id == 52)
            n = (int)fread(vm->memory + b, 1, (size_t)c, g_files[a]);
        else
            n = (int)fwrite(vm->memory + b, 1, (size_t)c, g_files[a]);
        return clvm_vm_push64(vm, n) ? 0 : -1;
    }
    return clvm_vm_push64(vm, 0) ? 0 : -1;
}

static int run_image(const char *path) {
    FILE *f;
    uint8_t *buf;
    long n;
    ClvmImage image;
    ClvmVm vm;
    ClvmStepResult r;
    int steps;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "missing %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 1;
    }
    fclose(f);
    if (clvm_parse(buf, (size_t)n, &image) != CL_LOAD_OK) {
        fprintf(stderr, "bad clv %s\n", path);
        return 1;
    }
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &image, sys, 0);
    r = CLVM_STEP_SLICE;
    for (steps = 0; steps < 8000 && r == CLVM_STEP_SLICE; ++steps)
        r = clvm_step(&vm, 200000u);
    if (r == CLVM_STEP_FAULT) {
        fprintf(stderr, "fault %s pc=%u fpc=%u\n", clvm_fault_text(vm.fault),
                vm.pc, vm.fault_pc);
        return 1;
    }
    if (r != CLVM_STEP_HALT) {
        fprintf(stderr, "stopped result=%d pc=%u steps=%d\n", (int)r, vm.pc, steps);
        return 1;
    }
    printf("halt %s pc=%u\n", path, vm.pc);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    return run_image(argv[1]);
}
