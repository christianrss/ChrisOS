#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

/* We need access to nothing static — scan native bytes after CALL32 setup. */

int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    uint32_t *table;
    uint32_t call_pcs[] = {130909, 130977, 131005, 131022, 131072, 131124, 288025};
    uint32_t call_tgts[] = {7749, 4957, 7749, 5099, 1143, 130109, 130841};
    uint32_t i, j;

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

    for (i = 0; i < sizeof(call_pcs) / sizeof(call_pcs[0]); i++) {
        uint32_t pc = call_pcs[i];
        uint32_t want = call_tgts[i];
        uint32_t off = table[pc];
        uint32_t tgt_off = table[want];
        uint8_t *nat = buf.w + off;
        int found = 0;
        printf("CALL32 @%u -> %u  nat=%u tgt_nat=%u\n", pc, want, off, tgt_off);
        /* Scan first 80 bytes of native for E9 jmp; last E9 before end is likely the call jump */
        for (j = 0; j + 5 < 120u; j++) {
            if (nat[j] == 0xE9) {
                int32_t rel;
                uint32_t abs;
                memcpy(&rel, nat + j + 1, 4);
                abs = off + j + 5u + (uint32_t)rel;
                printf("  E9 @+%u rel=%d abs=%u %s\n", j, (int)rel, abs,
                       abs == tgt_off ? "OK" : (abs == table[pc + 5] ? "FALLTHROUGH!" : "OTHER"));
                found = 1;
            }
        }
        if (!found)
            printf("  no E9 found\n");
        /* Also dump first 32 bytes */
        printf("  bytes:");
        for (j = 0; j < 48 && j < 200; j++)
            printf(" %02x", nat[j]);
        printf("\n");
    }
    return 0;
}
