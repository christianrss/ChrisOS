#include "chrismake.h"
#include <stdio.h>
#include <string.h>

static char g_ran[192];

static int rec_cb(void *user, const char *recipe, char *err, int err_cap) {
    (void)user;
    (void)err;
    (void)err_cap;
    strncpy(g_ran, recipe, sizeof(g_ran) - 1u);
    g_ran[sizeof(g_ran) - 1u] = 0;
    return 1;
}

static const char mk[] =
    "CC=cc\n"
    "all: Foo.CLV\n"
    "Foo.CLV: Foo.LST\n"
    "\t$(CC) $<\n"
    "foo.clv: foo.lst\n"
    "\tcc foo.lst\n";

int main(void) {
    char rec[192];
    if (!chrismake_first_recipe(mk, "Foo.CLV", rec, (int)sizeof(rec))) {
        fprintf(stderr, "test_chrismake: no recipe Foo.CLV\n");
        return 1;
    }
    if (strcmp(rec, "cc Foo.LST") != 0) {
        fprintf(stderr, "test_chrismake: Foo.CLV got '%s'\n", rec);
        return 1;
    }
    if (!chrismake_first_recipe(mk, "foo.clv", rec, (int)sizeof(rec))) {
        fprintf(stderr, "test_chrismake: no recipe foo.clv\n");
        return 1;
    }
    if (strcmp(rec, "cc foo.lst") != 0) {
        fprintf(stderr, "test_chrismake: foo.clv got '%s'\n", rec);
        return 1;
    }
    rec[0] = 0;
    if (chrismake_first_recipe(mk, "FOO.CLV", rec, (int)sizeof(rec))) {
        fprintf(stderr, "test_chrismake: FOO.CLV must not match Foo.CLV\n");
        return 1;
    }
    {
        static const char phony[] =
            ".PHONY: all\n"
            "all: Foo.CLV\n"
            "Foo.CLV: Foo.LST\n"
            "\t$(CC) $<\n"
            "CC=cc\n";
        if (!chrismake_first_recipe(phony, "Foo.CLV", rec, (int)sizeof(rec))) {
            fprintf(stderr, "test_chrismake: phony all hid Foo.CLV\n");
            return 1;
        }
        if (strcmp(rec, "cc Foo.LST") != 0) {
            fprintf(stderr, "test_chrismake: phony recipe '%s'\n", rec);
            return 1;
        }
        g_ran[0] = 0;
        {
            char err[160];
            err[0] = 0;
            if (!chrismake_run(phony, "", rec_cb, 0, err, (int)sizeof(err))) {
                fprintf(stderr, "test_chrismake: all failed %s\n", err);
                return 1;
            }
        }
        if (strcmp(g_ran, "cc Foo.LST") != 0) {
            fprintf(stderr, "test_chrismake: all ran '%s'\n", g_ran);
            return 1;
        }
    }
    {
        char err[160];
        g_ran[0] = 0;
        err[0] = 0;
        if (!chrismake_run("all:\n", "Auto.CLV", rec_cb, 0, err,
                           (int)sizeof(err))) {
            fprintf(stderr, "test_chrismake: implicit failed %s\n", err);
            return 1;
        }
        if (strcmp(g_ran, "cc -c Auto.LST") != 0) {
            fprintf(stderr, "test_chrismake: implicit ran '%s'\n", g_ran);
            return 1;
        }
    }
    printf("test_chrismake: ok\n");
    return 0;
}
