#include "chrisasm.h"
#include "chrisld.h"
#include "chriso.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    ChrisoImage img;
    uint8_t elf[512];
    uint64_t entry;
    int n;

    if (chrisasm_assemble("main:\nmov rax, 0\nret\n", &img) != 0) {
        fprintf(stderr, "asm failed\n");
        return 1;
    }
    n = chrisld_link(&img, 0xffffffff80000000ull, elf, sizeof(elf), &entry);
    if (n < 64) {
        fprintf(stderr, "link failed\n");
        return 1;
    }
    if (elf[0] != 0x7fu || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        fprintf(stderr, "bad magic\n");
        return 1;
    }
    if (chrisld_validate(elf, (uint32_t)n) != 0) {
        fprintf(stderr, "elf validate failed\n");
        return 1;
    }
    {
        ChrisoImage caller;
        ChrisoImage callee;
        const ChrisoImage *objs[2];
        uint32_t disp;

        if (chrisasm_assemble("main:\ncall foo\nret\n", &caller) != 0 ||
            chrisasm_assemble("foo:\nmov rax, 42\nret\n", &callee) != 0) {
            fprintf(stderr, "multi asm failed\n");
            return 1;
        }
        objs[0] = &caller;
        objs[1] = &callee;
        n = chrisld_link_objects(objs, 2u, 0x400000ull, elf, sizeof(elf), &entry);
        if (n < 64 || chrisld_validate(elf, (uint32_t)n) != 0) {
            fprintf(stderr, "multi link failed\n");
            return 1;
        }
        disp = (uint32_t)elf[128 + 1] |
               ((uint32_t)elf[128 + 2] << 8) |
               ((uint32_t)elf[128 + 3] << 16) |
               ((uint32_t)elf[128 + 4] << 24);
        if (disp != 11u) {
            fprintf(stderr, "call displacement %u\n", disp);
            return 1;
        }
        if (chrisld_link(&caller, 0x400000ull, elf, sizeof(elf), &entry) >= 0) {
            fprintf(stderr, "undefined symbol was linked\n");
            return 1;
        }
        if (chrisasm_assemble("foo:\nret\n", &caller) != 0) {
            fprintf(stderr, "dup asm failed\n");
            return 1;
        }
        objs[0] = &caller;
        objs[1] = &callee;
        if (chrisld_link_objects(objs, 2u, 0x400000ull, elf, sizeof(elf),
                                 &entry) >= 0) {
            fprintf(stderr, "duplicate symbol was linked\n");
            return 1;
        }
    }
    puts("test_chrisld: ok");
    return 0;
}
