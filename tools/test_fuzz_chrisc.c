#include "chrisc.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_seed = 0xC0FFEEu;

static uint32_t rnd(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

int main(void) {
    static ChrisResult r;
    static uint8_t code[256 * 1024];
    char src[240];
    const char *oksrc = "int main(){return 1;}\n";
    int i;

    for (i = 0; i < 48; ++i) {
        int n = (int)(rnd() % 180u);
        int k;
        int rc;
        for (k = 0; k < n; ++k) {
            src[k] = (char)(32 + (rnd() % 95u));
        }
        src[n] = 0;
        memset(&r, 0, sizeof(r));
        rc = chrisc_compile(src, (size_t)n, code, sizeof(code), &r);
        if (rc != 0 && rc != 1) {
            fprintf(stderr, "fuzz chrisc: return %d at %d\n", rc, i);
            return 1;
        }
    }
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile(oksrc, strlen(oksrc), code, sizeof(code), &r)) {
        fprintf(stderr, "fuzz chrisc: valid source rejected %s\n", r.diag.message);
        return 1;
    }
    puts("test_fuzz_chrisc: ok");
    return 0;
}
