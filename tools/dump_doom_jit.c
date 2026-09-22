#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

void serial_puts(const char *s) { fputs(s, stderr); }
void serial_write_u64(uint64_t v) {
    fprintf(stderr, "%llu", (unsigned long long)v);
}

int main(void) {
    FILE *f;
    uint8_t *file;
    long sz;
    ClvmImage img;
    JitBuf buf;
    JitFn fn;
    uint32_t *table;
    uint32_t off;
    uint32_t i;

    f = fopen("GAMES/DOOM/ENGINE.CLV", "rb");
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
    off = table[img.entry];
    printf("entry=%u off=%u\n", img.entry, off);
    printf("native at entry:\n");
    for (i = 0; i < 80; i++)
        printf("%02x%s", buf.w[off + i], ((i + 1) % 16) ? " " : "\n");
    printf("\nfirst 160 bytes of blob:\n");
    for (i = 0; i < 160; i++)
        printf("%02x%s", buf.w[i], ((i + 1) % 16) ? " " : "\n");
    printf("\n");
    return 0;
}
