#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "clvm.h"
#include "jit.h"
#include "jit_compile.h"

void serial_puts(const char *s) { (void)s; }
void serial_write_u64(uint64_t v) { (void)v; }

/*
 * Instrument: after full compile, check every 0F 8D (jge) whether it targets
 * the fault stub (~offset 20). Also check 0F 84/85/82/87/88 near early area.
 */
int main(void) {
    FILE *f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
    long sz;
    uint8_t *file;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    uint32_t *table;
    uint32_t i, bad = 0, ok = 0, checked = 0;
    uint32_t fault_off = 20; /* approximate; discover from blob */
    uint32_t code_end;

    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)sz);
    fread(file, 1, (size_t)sz, f); fclose(f);
    clvm_parse(file, (size_t)sz, &img);
    memset(&buf, 0, sizeof(buf));
    if (jit_compile_image(&img, &buf, &fn) != 0)
        return 1;

    table = (uint32_t *)(void *)(buf.w + (buf.used - img.code_size * 4u));
    code_end = buf.used - img.code_size * 4u;

    /* discover fault: look for mov dword [rbx+..], 4 ; mov eax, 3 */
    for (i = 0; i + 16 < 80; i++) {
        if (buf.w[i] == 0xC7 && buf.w[i + 1] == 0x83 &&
            buf.w[i + 6] == 4 && buf.w[i + 7] == 0 &&
            buf.w[i + 10] == 0xB8 && buf.w[i + 11] == 3) {
            fault_off = i;
            break;
        }
    }
    printf("fault_off=%u code_end=%u\n", fault_off, code_end);

    /* Scan native code region for jge (0F 8D) and see targets */
    for (i = 0; i + 6 < code_end; i++) {
        int32_t rel;
        uint32_t tgt;
        if (buf.w[i] != 0x0F || buf.w[i + 1] != 0x8D)
            continue;
        rel = (int32_t)(buf.w[i+2] | (buf.w[i+3]<<8) | (buf.w[i+4]<<16) |
                        (buf.w[i+5]<<24));
        tgt = i + 6u + (uint32_t)rel;
        checked++;
        if (tgt == fault_off)
            ok++;
        else {
            bad++;
            if (bad <= 15)
                printf("BAD jge at %u -> %u (rel=%d) expect %u\n",
                       i, tgt, (int)rel, fault_off);
        }
    }
    printf("jge checked=%u ok=%u bad=%u\n", checked, ok, bad);

    /* Specifically entry's jge */
    {
        uint32_t off = table[img.entry];
        uint32_t j;
        for (j = 0; j + 6 < 40; j++) {
            if (buf.w[off+j]==0x0F && buf.w[off+j+1]==0x8D) {
                int32_t rel = (int32_t)(buf.w[off+j+2]|(buf.w[off+j+3]<<8)|
                    (buf.w[off+j+4]<<16)|(buf.w[off+j+5]<<24));
                uint32_t tgt = off + j + 6 + (uint32_t)rel;
                printf("entry jge off=%u site=%u -> %u\n", off, off+j, tgt);
                break;
            }
        }
    }
    return bad ? 2 : 0;
}
