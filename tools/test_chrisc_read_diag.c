#include "chrisc.h"

#include <stdio.h>
#include <string.h>

static int read_selective(void *user, const char *path, char *out, int cap) {
    const char *src = "int x;\n";
    int n;
    (void)user;
    if (!path || strcmp(path, "OK.CC") != 0) {
        return -1;
    }
    n = (int)strlen(src);
    if (n >= cap) {
        return -1;
    }
    memcpy(out, src, (size_t)n + 1u);
    return n;
}

static int expect_missing(const char **paths, int npaths, const char *want) {
    static ChrisResult r;
    static uint8_t code[256 * 1024];
    memset(&r, 0, sizeof(r));
    if (chrisc_compile_files_ex(paths, npaths, read_selective, 0, code,
                                sizeof(code), &r, 0, 0)) {
        fprintf(stderr, "read diag: compile succeeded for %s\n", want);
        return 1;
    }
    if (strcmp(r.diag.file, want) != 0 ||
        strcmp(r.diag.message, "cannot read source file") != 0 ||
        r.diag.line != 1) {
        fprintf(stderr, "read diag: got %s:%d %s, want %s\n",
                r.diag.file, r.diag.line, r.diag.message, want);
        return 1;
    }
    return 0;
}

int main(void) {
    const char *one[] = { "NO/SUCH.CC" };
    const char *two[] = { "OK.CC", "MISSING/SECOND.CC" };
    if (expect_missing(one, 1, "NO/SUCH.CC") != 0) {
        return 1;
    }
    if (expect_missing(two, 2, "MISSING/SECOND.CC") != 0) {
        return 1;
    }
    puts("test_chrisc_read_diag: ok");
    return 0;
}
