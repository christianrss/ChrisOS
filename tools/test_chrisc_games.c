#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"

static int compile_file(const char *path) {
    static char src[65536];
    uint8_t code[65535];
    ChrisResult res;
    FILE *f = fopen(path, "rb");
    size_t n;

    assert(f != 0);
    n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = 0;
    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, n, code, sizeof(code), &res)) {
        fprintf(stderr, "%s: %d:%d %s\n", path, res.diag.line, res.diag.column,
                res.diag.message);
        return 0;
    }
    return 1;
}

int main(void) {
    static const char src[] =
        "void main(){\n"
        " int w;\n"
        " int h;\n"
        " viewport(0,0);\n"
        " viewport(960,540);\n"
        " w=screen_w();\n"
        " h=screen_h();\n"
        "}\n";
    uint8_t code[4096];
    ChrisResult res;

    assert(compile_file("GAMES/CUBE.CC"));
    assert(compile_file("GAMES/WORLD.CC"));
    assert(compile_file("GAMES/SPHERE.CC"));
    assert(compile_file("GAMES/WATCH.CC"));
    memset(&res, 0, sizeof(res));
    assert(chrisc_compile(src, strlen(src), code, sizeof(code), &res));
    puts("test_chrisc_games: ok");
    return 0;
}
