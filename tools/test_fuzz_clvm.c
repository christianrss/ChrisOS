#include "clvm.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_seed = 0xC1A55Eu;

static uint32_t rnd(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

int main(void) {
    uint8_t buf[96];
    uint8_t image[64];
    uint8_t code[4] = { 0x01, 0x01, 0x00, 0x08 };
    ClvmImage parsed;
    size_t nwritten;
    int i;

    for (i = 0; i < 200; ++i) {
        int n = (int)(rnd() % (sizeof(buf) + 1u));
        int k;
        ClvmLoadError err;
        for (k = 0; k < n; ++k) {
            buf[k] = (uint8_t)(rnd() & 0xffu);
        }
        err = clvm_parse(n ? buf : 0, (size_t)n, &parsed);
        if ((int)err < (int)CL_LOAD_OK || (int)err > (int)CL_LOAD_OUTPUT) {
            fprintf(stderr, "fuzz clvm: error %d\n", (int)err);
            return 1;
        }
        if (!clvm_load_error(err)) {
            fprintf(stderr, "fuzz clvm: null message\n");
            return 1;
        }
    }
    nwritten = clvm_write_image(image, sizeof(image), 0, 0, code, sizeof(code));
    if (nwritten == 0 ||
        clvm_parse(image, nwritten, &parsed) != CL_LOAD_OK ||
        parsed.code_size != sizeof(code)) {
        fprintf(stderr, "fuzz clvm: valid image rejected\n");
        return 1;
    }
    puts("test_fuzz_clvm: ok");
    return 0;
}
