#!/usr/bin/env python3
"""Host: run ENGINE string-init with interpreter, dump string memory."""
import ctypes
import subprocess
import sys
from pathlib import Path

# Prefer a small C harness compiled on the fly
ROOT = Path("/mnt/e/Aulas/ChrisOS")
src = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "clvm.h"
#include "clvm_vm.h"

void serial_puts(const char *s) { (void)s; }
void serial_write_u64(uint64_t v) { (void)v; }
void serial_putc(char c) { (void)c; }

static int sys_stub(ClvmVm *vm, int32_t id, void *user) {
    (void)user;
    if (id == 53) {
        int32_t fd, addr, n;
        if (!clvm_vm_pop(vm, &n) || !clvm_vm_pop(vm, &addr) || !clvm_vm_pop(vm, &fd))
            return -1;
        if ((fd == 1 || fd == 2) && n > 0 && addr >= 0 &&
            (uint64_t)addr + (uint32_t)n <= vm->mem_size) {
            fwrite(vm->memory + (uint32_t)addr, 1, (size_t)n, stdout);
        }
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (id == 12) { /* wait */
        int32_t t; clvm_vm_pop(vm, &t);
        return clvm_vm_push(vm, 0) ? 0 : -1;
    }
    if (id == 56) {
        int32_t sz; uint64_t p;
        if (!clvm_vm_pop(vm, &sz)) return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)(uint32_t)sz, &p))
            return clvm_vm_push(vm, 0) ? 0 : -1;
        return clvm_vm_push(vm, (int32_t)p) ? 0 : -1;
    }
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz; uint8_t *file; ClvmImage img; ClvmVm vm;
    uint32_t addr = 472880;
    uint32_t steps = 0;
    if (!f) return 1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK) return 2;
    memset(&vm, 0, sizeof(vm));
    clvm_vm_init(&vm, &img, sys_stub, 0);
    /* force large heap like doom */
    {
        uint8_t *mem = calloc(1, 32u * 1024u * 1024u);
        clvm_vm_set_memory(&vm, mem, 32ull * 1024ull * 1024ull);
    }
    printf("entry=%u heap_off=%llu\n", img.entry, (unsigned long long)vm.heap_off);
    /* Run until PC leaves string-init / ginits — stop when pc < entry (jumped to main)
       or after enough STOREBs. Main is before entry in code layout. */
    while (steps < 2000000u && vm.state == CLVM_READY) {
        uint32_t pc0 = vm.pc;
        ClvmStepResult r = clvm_vm_step(&vm);
        steps++;
        if (r != CLVM_STEP_OK && r != CLVM_STEP_YIELD) {
            printf("step fail r=%d fault=%d pc=%u steps=%u\n",
                   (int)r, (int)vm.fault, vm.pc, steps);
            break;
        }
        /* After init, JMP to main (pc < entry) */
        if (vm.pc < img.entry && pc0 >= img.entry) {
            printf("jumped to main pc=%u after %u steps\n", vm.pc, steps);
            break;
        }
    }
    printf("mem[472880]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
           vm.memory[addr], vm.memory[addr+1], vm.memory[addr+2], vm.memory[addr+3],
           vm.memory[addr+4], vm.memory[addr+5], vm.memory[addr+6], vm.memory[addr+7],
           vm.memory[addr+8], vm.memory[addr+9], vm.memory[addr+10], vm.memory[addr+11]);
    printf("cstr: [%s]\n", (char *)vm.memory + addr);
    return 0;
}
'''
Path('/tmp/test_strinit.c').write_text(src)
cmd = [
    'gcc', '-std=c11', '-O2', '-o', '/tmp/test_strinit',
    '/tmp/test_strinit.c',
    'compiler/clvm/clvm_vm.c', 'compiler/clvm/clvm_format.c',
    '-Icompiler/clvm', '-Icompiler', '-Ikernel/metal',
    '-DCLVM_HOST=1',
]
# check how other tests compile
print('compiling...')
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
if r.returncode != 0:
    print(r.stderr[-2000:])
    # try without CLVM_HOST
    cmd2 = [c for c in cmd if c != '-DCLVM_HOST=1']
    r = subprocess.run(cmd2, cwd=ROOT, capture_output=True, text=True)
    print('retry', r.returncode)
    print(r.stderr[-2000:])
    if r.returncode != 0:
        sys.exit(1)
print(subprocess.check_output(['/tmp/test_strinit'], cwd=ROOT, text=True)[:2000])
