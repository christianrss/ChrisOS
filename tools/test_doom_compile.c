#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chrisc.h"

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

static int read_lst(const char *path, const char **ps, int max) {
    static char buf[16384];
    static char *lines[128];
    FILE *f;
    size_t n;
    int i;
    int count;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    count = 0;
    i = 0;
    lines[count++] = buf;
    while (i < (int)n && count < max) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = 0;
            if (buf[i + 1] && buf[i + 1] != '\n' && buf[i + 1] != '\r')
                lines[count++] = &buf[i + 1];
        }
        i++;
    }
    for (i = 0; i < count; i++)
        ps[i] = lines[i];
    return count;
}

int main(void) {
    const char *ps[128];
    int n;
    uint8_t *code;
    ChrisResult r;
    code = malloc(4u * 1024u * 1024u);
    if (!code)
        return 1;
    n = read_lst("GAMES/DOOM/DOOM.LST", ps, 128);
    if (n < 2) {
        fprintf(stderr, "doom lst fail\n");
        return 1;
    }
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile_files(ps, n, read_file, 0, code, 4u * 1024u * 1024u, &r)) {
        fprintf(stderr, "doom compile fail %s:%d %s\n", r.diag.file, r.diag.line,
                r.diag.message);
        return 1;
    }
    printf("test_doom_compile: ok code=%u entry=%u files=%d\n",
           (unsigned)r.code_size, r.entry, n);
    ps[0] = "GAMES/PHYS.CC";
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile_files(ps, 1, read_file, 0, code, 4u * 1024u * 1024u, &r)) {
        fprintf(stderr, "phys compile fail %s:%d %s\n", r.diag.file, r.diag.line,
                r.diag.message);
        return 1;
    }
    printf("test_phys_compile: ok code=%u\n", (unsigned)r.code_size);
    free(code);
    return 0;
}
