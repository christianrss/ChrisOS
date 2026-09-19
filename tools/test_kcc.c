#include "kcc.h"
#include "chriso.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;

    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

int main(void) {
    ChrisoImage img;
    char *src = read_file("kernel/metal/serial.c");

    if (!src) {
        fprintf(stderr, "cannot read serial.c\n");
        return 1;
    }
    if (kcc_compile_source(src, &img) != 0) {
        fprintf(stderr, "kcc failed\n");
        free(src);
        return 1;
    }
    if (img.sec_size[CHRISO_SEC_TEXT] == 0u) {
        fprintf(stderr, "empty text\n");
        free(src);
        return 1;
    }
    free(src);
    puts("test_kcc: ok");
    return 0;
}
