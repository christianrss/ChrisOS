#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "clvm.h"
#include "clvm_vm.h"

void serial_puts(const char *s) { (void)s; }
void serial_write_u64(uint64_t v) { (void)v; }
void serial_putc(char c) { (void)c; }
void serial_write_hex(uint64_t v) { (void)v; }
void gc_poll(void) {}

static int sys_stub(ClvmVm *vm, int32_t id, void *user) {
    (void)user;
    if (id == 53) {
        int32_t fd, addr, n;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) || !clvm_vm_pop(vm, &fd))
            return -1;
        if ((fd == 1 || fd == 2) && n > 0 && addr >= 0 &&
            (uint64_t)addr + (uint32_t)n <= vm->mem_size) {
            printf("\n[fwrite pc=%u csp=%u fd=%d addr=%d n=%d mem=", vm->pc,
                   vm->csp, fd, addr, n);
            if (vm->csp > 0)
                printf("ret=%u ", vm->calls[vm->csp - 1]);
            if (vm->csp > 1)
                printf("ret2=%u ", vm->calls[vm->csp - 2]);
            if (vm->csp > 2)
                printf("ret3=%u ", vm->calls[vm->csp - 3]);
            for (int i = 0; i < n && i < 20; i++)
                printf("%02x", vm->memory[(uint32_t)addr + (uint32_t)i]);
            printf("] ");
            fwrite(vm->memory + (uint32_t)addr, 1, (size_t)n, stdout);
        }
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (id == 12) {
        int32_t t;
        clvm_vm_pop(vm, &t);
        return clvm_vm_push(vm, 0) ? 0 : -1;
    }
    if (id == 56) {
        int32_t sz;
        uint64_t p;
        if (!clvm_vm_pop(vm, &sz))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)(uint32_t)sz, &p))
            return clvm_vm_push(vm, 0) ? 0 : -1;
        return clvm_vm_push(vm, (int32_t)p) ? 0 : -1;
    }
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    ClvmVm vm;
    uint32_t addr = 472880;
    uint32_t steps = 0;
    uint8_t *mem;
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz)
        return 1;
    fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK)
        return 2;
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, sys_stub, 0);
    mem = calloc(1, 32u * 1024u * 1024u);
    clvm_vm_set_memory(&vm, mem, 32ull * 1024ull * 1024ull);
    printf("entry=%u heap_off=%llu\n", img.entry,
           (unsigned long long)vm.heap_off);
    while (steps < 5000u && vm.state == CLVM_READY) {
        uint32_t pc0 = vm.pc;
        ClvmStepResult r = clvm_step(&vm, 100000u);
        steps++;
        if (pc0 >= img.entry && vm.pc < 434980u && vm.pc >= 434850u) {
            printf("enter main-area pc0=%u pc=%u steps=%u\n", pc0, vm.pc,
                   steps);
            printf("mem[472880]=");
            for (int i = 0; i < 12; i++)
                printf("%02x ", vm.memory[addr + (uint32_t)i]);
            printf("\n");
        }
        if (r == CLVM_STEP_HALT) {
            printf("halt pc=%u batches=%u\n", vm.pc, steps);
            break;
        }
        if (r == CLVM_STEP_FAULT) {
            printf("fault=%d pc=%u batches=%u\n", (int)vm.fault, vm.pc, steps);
            break;
        }
        if (vm.pc < img.entry && pc0 >= img.entry) {
            printf("jumped down pc0=%u pc=%u after %u batches\n", pc0, vm.pc,
                   steps);
            if (steps > 5)
                break;
        }
    }
    printf("mem[472880]=");
    for (int i = 0; i < 12; i++)
        printf("%02x ", vm.memory[addr + (uint32_t)i]);
    printf("\ncstr: [%s]\n", (char *)vm.memory + addr);
    /* also dump PACKAGE-like Doom Generic */
    {
        uint32_t a = 437276 + 4191;
        printf("Doom Generic at %u: [%s]\n", a, (char *)vm.memory + a);
    }
    return 0;
}
