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

static int compile_lst(const char *lst_path) {
    static char lst[8192];
    static char paths[32][128];
    const char *pp[32];
    static uint8_t code[524288];
    ChrisResult res;
    FILE *f;
    size_t n;
    int npaths = 0;
    int p;
    int i;
    f = fopen(lst_path, "rb");
    if (!f) {
        fprintf(stderr, "missing %s\n", lst_path);
        return 0;
    }
    n = fread(lst, 1, sizeof(lst) - 1, f);
    fclose(f);
    lst[n] = 0;
    p = 0;
    while (p < (int)n && npaths < 32) {
        int j = 0;
        while (p < (int)n && (lst[p] == ' ' || lst[p] == '\t' || lst[p] == '\r' ||
                              lst[p] == '\n')) {
            p++;
        }
        if (p >= (int)n) {
            break;
        }
        if (lst[p] == '#') {
            while (p < (int)n && lst[p] != '\n') {
                p++;
            }
            continue;
        }
        while (p < (int)n && lst[p] != '\n' && lst[p] != '\r' && j < 127) {
            paths[npaths][j++] = lst[p++];
        }
        while (j > 0 && (paths[npaths][j - 1] == ' ' ||
                         paths[npaths][j - 1] == '\t')) {
            j--;
        }
        paths[npaths][j] = 0;
        if (j > 0) {
            pp[npaths] = paths[npaths];
            npaths++;
        }
        if (p < (int)n && (lst[p] == '\n' || lst[p] == '\r')) {
            p++;
        }
    }
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile_files(pp, npaths, read_file, 0, code, sizeof(code),
                              &res)) {
        fprintf(stderr, "%s: %s:%d:%d %s\n", lst_path, res.diag.file,
                res.diag.line, res.diag.column, res.diag.message);
        return 0;
    }
    if (res.code_size == 0) {
        fprintf(stderr, "%s: empty code\n", lst_path);
        return 0;
    }
    (void)i;
    printf("%s: %u bytes\n", lst_path, (unsigned)res.code_size);
    return 1;
}

int main(void) {
    const char *lists[] = {
        "APPS/DESKTOP/DESKTOP.LST", "APPS/TASKBAR/TASKBAR.LST",
        "APPS/SHELL/SHELL.LST",     "APPS/EXPLORER/EXPLORER.LST",
        "APPS/EDITOR/EDITOR.LST",   "APPS/TASKMGR/TASKMGR.LST",
        "APPS/BALL/BALL.LST",       "APPS/PREFS/PREFS.LST"};
    int i;
    for (i = 0; i < 8; ++i) {
        if (!compile_lst(lists[i])) {
            return 1;
        }
    }
    puts("test_chrisc_apps: ok");
    return 0;
}
