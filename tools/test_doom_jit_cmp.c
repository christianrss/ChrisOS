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

static void grow(ClvmVm *vm) {
    uint8_t *big = calloc(32u * 1024u * 1024u, 1);
    clvm_vm_set_memory(vm, big, 32u * 1024u * 1024u);
}

/* Run interp to target PC; return 0 on success. */
static int interp_to(ClvmVm *vm, uint32_t target, uint32_t max_steps) {
    uint32_t steps = 0;
    while (vm->pc != target && vm->state != CLVM_FAULTED &&
           vm->state != CLVM_HALTED && steps < max_steps) {
        ClvmStepResult r = clvm_step(vm, 1);
        steps++;
        if (r == CLVM_STEP_FAULT)
            return -1;
        if (r == CLVM_STEP_YIELD) {
            vm->state = CLVM_READY;
            vm->wake_tick = 0;
        }
    }
    return vm->pc == target ? 0 : -2;
}

/*
 * Patch native at target PC to: mov eax, [sp-related]; return sp and stack tops
 * via vm fields, return SLICE.
 * Actually: just leave; return with eax=sp, and don't destroy stack —
 * read sp into eax.
 */
static void patch_report_sp(uint8_t *nat) {
    /* movzx eax, word [rbx+0x810]; pop rbx; leave; ret */
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
    uint32_t target;
    ClvmVm vi, vj;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <pc>\n", argv[0]);
        return 1;
    }
    target = (uint32_t)strtoul(argv[1], 0, 0);

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 2;
    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    if (table[target] == 0) {
        printf("pc %u not an insn start (nat=0)\n", target);
        return 3;
    }
    patch_report_sp(buf.w + table[target]);
    __builtin___clear_cache((char *)buf.x + table[target],
                            (char *)buf.x + table[target] + 16);

    memset(&vi, 0, sizeof(vi));
    clvm_vm_init(&vi, &img, nosys, 0);
    grow(&vi);
    if (interp_to(&vi, target, 50000000u) != 0) {
        printf("interp failed pc=%u state=%d fault=%d\n",
               vi.pc, (int)vi.state, (int)vi.fault);
        return 4;
    }

    memset(&vj, 0, sizeof(vj));
    clvm_vm_init(&vj, &img, nosys, 0);
    grow(&vj);
    {
        int r = (int)fn(&vj, 500000000u, 0);
        printf("target=%u\n", target);
        printf("interp: sp=%u", vi.sp);
        if (vi.sp >= 1) printf(" top=%lld", (long long)vi.stack[vi.sp - 1]);
        if (vi.sp >= 2) printf(" 2nd=%lld", (long long)vi.stack[vi.sp - 2]);
        printf("\n");
        printf("jit:    r=%d state=%d sp=%u (eax_sp=%d)",
               r, (int)vj.state, vj.sp, r);
        if (vj.sp >= 1) printf(" top=%lld", (long long)vj.stack[vj.sp - 1]);
        if (vj.sp >= 2) printf(" 2nd=%lld", (long long)vj.stack[vj.sp - 2]);
        printf("\n");
        if (vi.sp != vj.sp)
            printf("DIVERGE sp\n");
        else if (vi.sp >= 1 && vi.stack[vi.sp - 1] != vj.stack[vj.sp - 1])
            printf("DIVERGE top\n");
        else
            printf("match\n");
    }
    return 0;
}
