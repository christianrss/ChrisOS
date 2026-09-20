#include <stdio.h>
#include <stdint.h>
#include "chrisc/chrisc.h"

static void hexdump(const uint8_t *code, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        printf("%02x ", code[i]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    FILE *f;
    char src[65536];
    size_t n;
    uint8_t code[65536];
    ChrisResult r;
    const char *path = argc > 1 ? argv[1] : "GAMES/CUBE.CC";

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open %s failed\n", path);
        return 1;
    }
    n = fread(src, 1, sizeof(src) - 1u, f);
    fclose(f);
    src[n] = 0;
    if (chrisc_compile(src, n, code, sizeof(code), &r) == 0 || r.code_size == 0) {
        fprintf(stderr, "compile fail: %s\n", r.diag.message);
        return 1;
    }
    printf("entry=%u size=%u\n", r.entry, (unsigned)r.code_size);
    hexdump(code, r.code_size);
    return 0;
}
