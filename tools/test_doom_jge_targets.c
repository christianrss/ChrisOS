#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "jit.h"
#include "jit_compile.h"

void serial_puts(const char *s) { fputs(s, stderr); }
void serial_write_u64(uint64_t v) {
    fprintf(stderr, "%llu", (unsigned long long)v);
}

/* We need to expose g_npatch - duplicate the check by counting branches ourselves */
static int insn_len(const uint8_t *code, uint32_t pc, uint32_t size) {
    uint8_t op;
    if (pc >= size) return 0;
    op = code[pc];
    switch (op) {
    case 0x01: case 0x2a: return 5; /* PUSH FPUSH */
    case 0x09: case 0x0a: case 0x13: case 0x0b: return 3;
    case 0x3c: case 0x3d: case 0x3e: return 2;
    case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x21: case 0x22: case 0x23: case 0x24: return 5;
    case 0x25: return 9;
    default: return 1;
    }
}

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    uint32_t pc, patches = 0, helpers = 0;
    uint32_t jge_sites = 0;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);

    pc = 0;
    while (pc < img.code_size) {
        uint8_t op = img.code[pc];
        int n = insn_len(img.code, pc, img.code_size);
        /* ops that use emit_jmp_pc / emit_jcc_pc */
        if (op == 0x09 || op == 0x0a || op == 0x13 || op == 0x0b ||
            op == 0x21 || op == 0x22 || op == 0x23 || op == 0x24)
            patches++;
        /* push/pop/store use jge to fault - not patches */
        if (op == 0x01 || op == 0x25 || op == 0x2a)
            jge_sites++;
        pc += (uint32_t)n;
    }
    printf("code_size=%u patches~=%u push_ops=%u JIT_MAX_PATCH=131072\n",
           img.code_size, patches, jge_sites);

    {
        JitBuf buf;
        JitFn fn;
        memset(&buf, 0, sizeof(buf));
        if (jit_compile_image(&img, &buf, &fn) != 0) {
            printf("compile failed\n");
            return 1;
        }
        printf("compile ok used=%u\n", buf.used);
        /* decode a few jge targets at various offsets */
        {
            uint32_t *table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
            uint32_t samples[] = {0, 100, 1000, 10000, img.entry, img.code_size / 2};
            uint32_t si;
            for (si = 0; si < 6; si++) {
                uint32_t spc = samples[si];
                uint32_t nat, j;
                /* find insn start at or after spc */
                while (spc < img.code_size && table[spc] == 0) spc++;
                if (spc >= img.code_size) continue;
                if (img.code[spc] != 0x01) continue; /* only PUSH */
                nat = table[spc];
                /* find 0f 8d in first 40 bytes */
                for (j = 0; j + 6 < 40; j++) {
                    if (buf.w[nat + j] == 0x0f && buf.w[nat + j + 1] == 0x8d) {
                        int32_t rel = (int32_t)(buf.w[nat+j+2] | (buf.w[nat+j+3]<<8) |
                            (buf.w[nat+j+4]<<16) | (buf.w[nat+j+5]<<24));
                        uint32_t tgt = nat + j + 6 + (uint32_t)rel;
                        printf("PUSH pc=%u nat=%u jge_target=%u (expect fault~20)\n",
                               spc, nat, tgt);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}
