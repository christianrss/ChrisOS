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

    if (chrisasm_assemble(".text\nmain:\nmov rax, 42\nret\n", &img) != 0) {
        fprintf(stderr, "asm failed\n");
        return 1;
    }
    n = chrisld_link(&img, 0x400000ull, elf, sizeof(elf), &entry);
    if (n < 64) {
        fprintf(stderr, "link failed\n");
        return 1;
    }
    if (elf[0] != 0x7fu || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        fprintf(stderr, "bad magic\n");
        return 1;
    }
    if (entry != 0x400000ull) {
        fprintf(stderr, "bad entry\n");
        return 1;
    }
    puts("test_native_link: ok");
    return 0;
}
