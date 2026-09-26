#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"

int main(int argc, char **argv) {
    FILE *f;
    char *src;
    long n;
    uint8_t *code;
    uint8_t *image;
    ChrisResult res;
    size_t bytes;
    if (argc != 3) {
        fprintf(stderr, "usage: cc_one in.cc out.clv\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    src = malloc((size_t)n + 1u);
    code = malloc(4u * 1024u * 1024u);
    image = malloc(4u * 1024u * 1024u + 64u);
    if (!src || !code || !image || fread(src, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 1;
    }
    fclose(f);
    src[n] = 0;
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, (size_t)n, code, 4u * 1024u * 1024u, &res) ||
        res.code_size == 0) {
        fprintf(stderr, "%s:%d:%d %s\n", argv[1], res.diag.line, res.diag.column,
                res.diag.message);
        return 1;
    }
    bytes = clvm_write_image(image, 4u * 1024u * 1024u + 64u, 0, res.entry,
                             code, res.code_size);
    if (!bytes)
        return 1;
    f = fopen(argv[2], "wb");
    if (!f || fwrite(image, 1, bytes, f) != bytes)
        return 1;
    fclose(f);
    printf("cc_one %s %zu\n", argv[2], bytes);
    return 0;
}
