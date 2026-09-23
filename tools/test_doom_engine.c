#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chrisc.h"

static int progress_count;

static void progress(void *user, int index, int total, const char *path) {
    (void)user;
    if (index == progress_count && total > 0 && path && path[0])
        progress_count++;
}

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

static int read_lst(const char *path, const char **ps, char *buf, int bufn, int max) {
    FILE *f;
    size_t n;
    int i;
    int count;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(buf, 1, (size_t)bufn - 1u, f);
    fclose(f);
    buf[n] = 0;
    count = 0;
    ps[count++] = buf;
    i = 0;
    while (i < (int)n && count < max) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = 0;
            if (i + 1 < (int)n && buf[i + 1] && buf[i + 1] != '\n' &&
                buf[i + 1] != '\r')
                ps[count++] = &buf[i + 1];
        }
        i++;
    }
    return count;
}

int main(void) {
    static char lst[16384];
    const char *ps[128];
    int n;
    uint8_t *code;
    ChrisResult r;
    code = malloc(8u * 1024u * 1024u);
    if (!code)
        return 1;
    n = read_lst("GAMES/DOOM/ENGINE.LST", ps, lst, (int)sizeof(lst), 128);
    if (n < 4) {
        fprintf(stderr, "engine lst fail n=%d\n", n);
        return 1;
    }
    memset(&r, 0, sizeof(r));
    progress_count = 0;
    if (!chrisc_compile_files_ex(ps, n, read_file, 0, code,
                                 8u * 1024u * 1024u, &r, progress, 0)) {
        fprintf(stderr, "test_doom_engine: fail %s:%d:%d %s (%d files)\n",
                r.diag.file, r.diag.line, r.diag.column, r.diag.message, n);
        free(code);
        return 1;
    }
    if (progress_count != n) {
        fprintf(stderr, "test_doom_engine: progress %d/%d\n", progress_count, n);
        free(code);
        return 1;
    }
    printf("test_doom_engine: compiled code=%u entry=%u files=%d\n",
           (unsigned)r.code_size, r.entry, n);
    free(code);
    return 0;
}
