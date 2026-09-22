#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)id;
    (void)user;
    return clvm_vm_push(vm, 0) ? 0 : -1;
}

static void grow(ClvmVm *vm) {
    uint8_t *big = calloc(32u * 1024u * 1024u, 1);
    clvm_vm_set_memory(vm, big, 32u * 1024u * 1024u);
}

/* Patch native site to: movzx eax,[rbx+sp]; pop rbx; leave; ret  — returns SP as step result */
static void patch_ret_sp(uint8_t *nat) {
    static const uint8_t stub[] = {
        0x0F, 0xB7, 0x83, 0x10, 0x08, 0x00, 0x00, /* movzx eax, word [rbx+0x810] */
        0x5B,                                     /* pop rbx */
        0xC9,                                     /* leave */
        0xC3                                      /* ret */
    };
    memcpy(nat, stub, sizeof(stub));
}

int main(int argc, char **argv) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    uint32_t *table;
    uint32_t target;
    ClvmVm vi, vj;
    uint32_t steps;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <pc>\n", argv[0]);
        return 1;
    }
    target = (uint32_t)strtoul(argv[1], 0, 0);

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f);
    fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK)
        return 1;

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    if (table[target] == 0) {
        printf("pc %u nat=0 (not insn start)\n", target);
        return 3;
    }
    patch_ret_sp(buf.w + table[target]);
    __builtin___clear_cache((char *)buf.x + table[target],
                            (char *)buf.x + table[target] + 16);

    memset(&vi, 0, sizeof(vi));
    clvm_vm_init(&vi, &img, nosys, 0);
    grow(&vi);
    for (steps = 0; steps < 50000000u && vi.pc != target; steps++) {
        ClvmStepResult r = clvm_step(&vi, 1);
        if (r == CLVM_STEP_FAULT) {
            printf("interp FAULT before target pc=%u sp=%u fault=%d steps=%u\n",
                   vi.pc, vi.sp, (int)vi.fault, steps);
            return 4;
        }
        if (r == CLVM_STEP_YIELD) {
            vi.state = CLVM_READY;
            vi.wake_tick = 0;
        }
        if (r == CLVM_STEP_HALT) {
            printf("interp HALT before target\n");
            return 4;
        }
    }
    if (vi.pc != target) {
        printf("interp never reached %u (pc=%u steps=%u)\n", target, vi.pc, steps);
        return 4;
    }

    memset(&vj, 0, sizeof(vj));
    clvm_vm_init(&vj, &img, nosys, 0);
    grow(&vj);
    {
        int r = (int)fn(&vj, 500000000u, 0);
        printf("target=%u\n", target);
        printf("interp: sp=%u", vi.sp);
        if (vi.sp >= 1)
            printf(" top=%lld", (long long)vi.stack[vi.sp - 1]);
        if (vi.sp >= 2)
            printf(" 2nd=%lld", (long long)vi.stack[vi.sp - 2]);
        printf(" pc=%u\n", vi.pc);
        printf("jit:    r=%d state=%d sp=%u pc=%u",
               r, (int)vj.state, vj.sp, vj.pc);
        if (vj.sp >= 1)
            printf(" top=%lld", (long long)vj.stack[vj.sp - 1]);
        if (vj.sp >= 2)
            printf(" 2nd=%lld", (long long)vj.stack[vj.sp - 2]);
        printf("\n");
        if (vj.pc != target)
            printf("JIT MISSED target (hit patch? r=sp?)\n");
        if (vi.sp != vj.sp)
            printf("DIVERGE sp\n");
        else if (vi.sp >= 1 && vi.stack[vi.sp - 1] != vj.stack[vj.sp - 1])
            printf("DIVERGE top\n");
        else
            printf("match\n");
    }
    return 0;
}
