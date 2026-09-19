#include "chrisasm.h"
#include "chriso.h"
#include <stdio.h>
#include <stdint.h>

int main(void) {
    ChrisoImage img;
    const char *src =
        ".text\n"
        "main:\n"
        "mov rax, 42\n"
        "ret\n";

    if (chrisasm_assemble(src, &img) != 0) {
        fprintf(stderr, "assemble failed\n");
        return 1;
    }
    if (img.sec_size[CHRISO_SEC_TEXT] < 3u) {
        fprintf(stderr, "text too small\n");
        return 1;
    }
    if (img.sec[CHRISO_SEC_TEXT][0] != 0x48u) {
        fprintf(stderr, "bad opcode\n");
        return 1;
    }
    puts("test_chrisasm: ok");
    return 0;
}
