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
    if (elf[56] != 1) {
        fprintf(stderr, "text-only elf should have one PT_LOAD\n");
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
    {
        ChrisoImage img;
        uint8_t big[8192];
        uint32_t disp;
        uint64_t rw;
        const char *src =
            ".text\n"
            "main:\n"
            "lea rax, [rel slot]\n"
            "ret\n"
            ".rodata\n"
            "msg:\n"
            ".asciz \"hi\"\n"
            ".bss\n"
            "slot:\n"
            ".zero 8\n";

        if (chrisasm_assemble(src, &img) != 0 || img.sec_size[CHRISO_SEC_RODATA] < 3u ||
            img.sec_size[CHRISO_SEC_BSS] != 8u) {
            fprintf(stderr, "section asm failed\n");
            return 1;
        }
        n = chrisld_link(&img, 0x400000ull, big, sizeof(big), &entry);
        if (n < 64 || chrisld_validate(big, (uint32_t)n) != 0) {
            fprintf(stderr, "section link failed\n");
            return 1;
        }
        if (big[56] != 2) {
            fprintf(stderr, "expected two PT_LOAD, got %u\n", big[56]);
            return 1;
        }
        if (big[120 + 4] != 6) {
            fprintf(stderr, "second segment is not RW\n");
            return 1;
        }
        rw = (uint64_t)big[120 + 16] | ((uint64_t)big[120 + 17] << 8) |
             ((uint64_t)big[120 + 18] << 16) | ((uint64_t)big[120 + 19] << 24) |
             ((uint64_t)big[120 + 20] << 32) | ((uint64_t)big[120 + 21] << 40) |
             ((uint64_t)big[120 + 22] << 48) | ((uint64_t)big[120 + 23] << 56);
        if (rw != 0x401000ull) {
            fprintf(stderr, "data vaddr %llx\n", (unsigned long long)rw);
            return 1;
        }
        disp = (uint32_t)big[4096 + 3] | ((uint32_t)big[4096 + 4] << 8) |
               ((uint32_t)big[4096 + 5] << 16) | ((uint32_t)big[4096 + 6] << 24);
        if (disp != 4089u) {
            fprintf(stderr, "bss lea disp %u\n", disp);
            return 1;
        }
        if (big[4096 + 16] != 'h' || big[4096 + 17] != 'i' || big[4096 + 18] != 0) {
            fprintf(stderr, "rodata bytes missing\n");
            return 1;
        }
    }
    puts("test_chrisld: ok");
    return 0;
}
