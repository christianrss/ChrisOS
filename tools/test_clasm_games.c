#include "clasm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int compile_file(const char *path) {
    static char src[65536];
    uint8_t code[65535];
    ClasmResult res;
    FILE *f = fopen(path, "rb");
    size_t n;

    assert(f != 0);
    n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = 0;
    memset(&res, 0, sizeof(res));
    if (!clasm_compile(src, n, code, sizeof(code), &res)) {
        fprintf(stderr, "%s: %d:%d %s\n", path, res.diag.line, res.diag.column,
                res.diag.message);
        return 0;
    }
    if (res.code_size == 0) {
        fprintf(stderr, "%s: empty bytecode\n", path);
        return 0;
    }
    return 1;
}

int main(void) {
    assert(compile_file("GAMES/BLINK.CVA"));
    puts("test_clasm_games: ok");
    return 0;
}
