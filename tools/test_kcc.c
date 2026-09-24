#include "kcc.h"
#include "chriso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int find_sym(const ChrisoImage *img, const char *name) {
    uint32_t i;
    for (i = 0; i < img->nsym; i++) {
        if (strcmp(img->sym[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    ChrisoImage img;
    const KccDiag *diag;
    char *src = read_file("tools/kcc_fixtures/level0.c");
    const char *serial_path = "kernel/metal/serial.c";

    if (!src) {
        fprintf(stderr, "cannot read level0 fixture\n");
        return 1;
    }
    if (kcc_compile_named("tools/kcc_fixtures/level0.c", src, &img) != 0) {
        diag = kcc_last_error();
        fprintf(stderr, "level0 failed: %s:%d:%d: %s\n", diag->file, diag->line,
                diag->column, diag->message);
        free(src);
        return 1;
    }
    free(src);
    diag = kcc_last_error();
    if (diag->severity != 0 || img.sec_size[CHRISO_SEC_TEXT] == 0u) {
        fprintf(stderr, "level0 produced no text\n");
        return 1;
    }
    if (!find_sym(&img, "kstart") || !find_sym(&img, "outb")) {
        fprintf(stderr, "level0 missing kstart or outb symbol\n");
        return 1;
    }
    /* call outb is still a defined symbol with a zero displacement.
     * A relocation count other than zero would be a different feature. */
    if (img.nrel != 0u) {
        fprintf(stderr, "level0 unexpectedly has relocations\n");
        return 1;
    }
    src = read_file(serial_path);
    if (!src) {
        fprintf(stderr, "cannot read serial.c\n");
        return 1;
    }
    if (kcc_compile_named(serial_path, src, &img) == 0) {
        fprintf(stderr, "serial.c was accepted\n");
        free(src);
        return 1;
    }
    free(src);
    diag = kcc_last_error();
    if (diag->severity != 1 || diag->line < 1 || diag->column < 1 ||
        diag->message[0] == 0 || strcmp(diag->file, serial_path) != 0) {
        fprintf(stderr, "serial.c diagnostic is incomplete\n");
        return 1;
    }
    if (strstr(diag->message, "preprocessor") == 0) {
        fprintf(stderr, "serial.c should fail on its preprocessor line: %s\n",
                diag->message);
        return 1;
    }
    puts("test_kcc: ok");
    return 0;
}
