#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

int main(void) {
    static char src[65536];
    uint8_t code[65535];
    ChrisResult res;
    FILE *f = fopen("SRC/CAT.CC", "rb");
    size_t n;

    assert(f != 0);
    n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = 0;
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile_ex("SRC/CAT.CC", src, n, read_file, 0, code, sizeof(code),
                           &res)) {
        fprintf(stderr, "%s:%d:%d %s\n", res.diag.file, res.diag.line,
                res.diag.column, res.diag.message);
        return 1;
    }
    assert(res.code_size > 0);
    puts("test_chrisc_include: ok");
    return 0;
}
