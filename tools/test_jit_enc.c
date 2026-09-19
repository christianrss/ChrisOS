#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>

#include "jit.h"
#include "jit_emit.h"

static int run_code(unsigned char *p, int expect) {
    int r;

    if (mprotect(p, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return -1;
    }
    r = ((int (*)(void))(void *)p)();
    return r == expect ? 0 : -1;
}

static int test_add(void) {
    JitBuf j;
    unsigned char *page;

    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return -1;
    }
    j.w = page;
    j.x = page;
    j.used = 0;
    j.cap = 4096;

    if (jit_emit_mov_r32_imm(&j, 0, 2) != 0 ||
        jit_emit_mov_r32_imm(&j, 3, 3) != 0 ||
        jit_emit_add_r32_r32(&j, 0, 3) != 0 ||
        jit_emit_ret(&j) != 0) {
        return -1;
    }
    return run_code(page, 5);
}

static int patch_rel32(unsigned char *site, unsigned char *target) {
    int32_t rel = (int32_t)(target - (site + 4));
    site[0] = (unsigned char)rel;
    site[1] = (unsigned char)(rel >> 8);
    site[2] = (unsigned char)(rel >> 16);
    site[3] = (unsigned char)(rel >> 24);
    return 0;
}

static int test_while(void) {
    JitBuf j;
    unsigned char *page;
    unsigned char *loop;
    unsigned char *done;
    unsigned char *je_site;

    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return -1;
    }
    j.w = page;
    j.x = page;
    j.used = 0;
    j.cap = 4096;

    if (jit_emit_prologue(&j, 1) != 0 ||
        jit_emit_mov_r32_imm(&j, 0, 0) != 0) {
        return -1;
    }
    loop = page + j.used;
    if (jit_emit_cmp_r32_imm8(&j, 0, 10) != 0 ||
        jit_emit_jge_rel32(&j, 0) != 0) {
        return -1;
    }
    je_site = page + j.used - 4;
    if (jit_emit_mov_r32_imm(&j, 3, 1) != 0 ||
        jit_emit_add_r32_r32(&j, 0, 3) != 0 ||
        jit_emit_jmp_rel32(&j, 0) != 0) {
        return -1;
    }
    patch_rel32(page + j.used - 4, loop);
    done = page + j.used;
    patch_rel32(je_site, done);
    if (jit_emit_epilogue(&j) != 0) {
        return -1;
    }
    return run_code(page, 10);
}

int main(void) {
    if (test_add() != 0) {
        fputs("test_jit_enc: add failed\n", stderr);
        return 1;
    }
    if (test_while() != 0) {
        fputs("test_jit_enc: while failed\n", stderr);
        return 1;
    }
    puts("test_jit_enc: ok");
    return 0;
}
