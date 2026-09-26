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

static int hits;

static int sys_stub(ClvmVm *vm, int32_t id, void *user) {
    (void)user;
    if (id == 53) {
        int32_t fd, addr, n;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) ||
            !clvm_vm_pop(vm, &fd))
            return -1;
        if (fd == 1 && hits++ < 3)
            printf("fwrite step-pc=%u addr=%d n=%d csp=%u ret=%u\n", vm->pc,
                   addr, n, vm->csp, vm->csp ? vm->calls[vm->csp - 1] : 0);
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (id == 56) {
        int32_t sz;
        uint64_t p;
        clvm_vm_pop(vm, &sz);
        clvm_guest_malloc(vm, (uint64_t)(uint32_t)sz, &p);
        return clvm_vm_push(vm, (int32_t)p) ? 0 : -1;
    }
    if (id == 12) {
        int32_t t;
        clvm_vm_pop(vm, &t);
        return clvm_vm_push(vm, 0) ? 0 : -1;
    }
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    ClvmVm vm;
    uint32_t i;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f);
    fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, sys_stub, 0);
    clvm_vm_set_memory(&vm, calloc(1, 32u * 1024u * 1024u),
                       32ull * 1024ull * 1024ull);
    printf("entry=%u\n", img.entry);
    for (i = 0; i < 500000u && vm.state == CLVM_READY; i++) {
        uint32_t pc0 = vm.pc;
        ClvmStepResult r = clvm_step(&vm, 1);
        if (pc0 >= img.entry && vm.pc < 434980u)
            printf("LEAVE INIT i=%u pc0=%u pc=%u\n", i, pc0, vm.pc);
        if (hits >= 1) {
            printf("first fwrite after insn %u; mem=", i);
            for (int k = 0; k < 12; k++)
                printf("%02x ", vm.memory[472880 + k]);
            printf("\n");
            break;
        }
        if (r == CLVM_STEP_FAULT) {
            printf("fault %d pc=%u i=%u\n", (int)vm.fault, vm.pc, i);
            break;
        }
    }
    return 0;
}
