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

static void poke64(ClvmVm *vm, uint64_t addr, uint64_t v) {
    memcpy(vm->memory + addr, &v, 8);
}

static void poke32(ClvmVm *vm, uint64_t addr, uint32_t v) {
    memcpy(vm->memory + addr, &v, 4);
}

static void setup_list(ClvmVm *vm, uint32_t ret_pc) {
    const uint64_t list_global = 271268;
    const uint64_t node = 4096;
    poke64(vm, node + 0, (uint64_t)ret_pc);
    poke32(vm, node + 8, 1);
    poke64(vm, node + 12, 0);
    poke64(vm, list_global, node);
}

static void patch_ret_sp(uint8_t *nat) {
    static const uint8_t stub[] = {
        0x0F, 0xB7, 0x83, 0x10, 0x08, 0x00, 0x00,
        0x5B, 0xC9, 0xC3
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
    ClvmVm vi, vj;
    uint32_t ret_pc = 434183;
    uint32_t start = 131142;
    uint32_t patch_pc = 131171;
    uint32_t i;

    if (argc > 1)
        patch_pc = (uint32_t)strtoul(argv[1], 0, 0);

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f);
    fclose(f);
    if (clvm_parse(file, (size_t)sz, &img) != CL_LOAD_OK)
        return 1;
    if (img.code[ret_pc] != CL_OP_RET) {
        for (i = 0; i < img.code_size; i++) {
            if (img.code[i] == CL_OP_RET) {
                ret_pc = i;
                break;
            }
        }
    }
    printf("ret_pc=%u patch_pc=%u\n", ret_pc, patch_pc);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    if (table[patch_pc] == 0) {
        printf("nat=0 at %u\n", patch_pc);
        return 3;
    }
    patch_ret_sp(buf.w + table[patch_pc]);
    __builtin___clear_cache((char *)buf.x + table[patch_pc],
                            (char *)buf.x + table[patch_pc] + 16);

    memset(&vi, 0, sizeof(vi));
    clvm_vm_init(&vi, &img, nosys, 0);
    grow(&vi);
    setup_list(&vi, ret_pc);
    vi.pc = start;
    for (i = 0; i < 100000u && vi.pc != patch_pc; i++) {
        ClvmStepResult r = clvm_step(&vi, 1);
        if (r == CLVM_STEP_FAULT) {
            printf("interp FAULT pc=%u sp=%u fault=%d steps=%u\n",
                   vi.pc, vi.sp, (int)vi.fault, i);
            return 4;
        }
        if (r == CLVM_STEP_YIELD) {
            vi.state = CLVM_READY;
            vi.wake_tick = 0;
        }
    }
    if (vi.pc != patch_pc) {
        printf("interp miss patch pc=%u sp=%u\n", vi.pc, vi.sp);
        return 4;
    }

    memset(&vj, 0, sizeof(vj));
    clvm_vm_init(&vj, &img, nosys, 0);
    grow(&vj);
    setup_list(&vj, ret_pc);
    vj.pc = start;
    {
        int r = (int)fn(&vj, 1000000u, 0);
        printf("interp: sp=%u", vi.sp);
        if (vi.sp)
            printf(" top=%lld", (long long)vi.stack[vi.sp - 1]);
        printf("\n");
        printf("jit:    r=%d sp=%u pc=%u state=%d", r, vj.sp, vj.pc, (int)vj.state);
        if (vj.sp)
            printf(" top=%lld", (long long)vj.stack[vj.sp - 1]);
        printf("\n");
        if (vi.sp == vj.sp &&
            (vi.sp == 0 || vi.stack[vi.sp - 1] == vj.stack[vj.sp - 1]))
            printf("match\n");
        else
            printf("DIVERGE\n");
    }
    return 0;
}
