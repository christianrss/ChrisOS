#include "chrisasm.h"
#include "chriso.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
    if (chrisasm_assemble("nope\n", &img) == 0) {
        fprintf(stderr, "unknown mnemonic was accepted\n");
        return 1;
    }
    if (chrisasm_assemble("main:\ncall foo\nret\n", &img) != 0) {
        fprintf(stderr, "call assemble failed\n");
        return 1;
    }
    if (img.nrel != 1u || img.rel[0].type != R_X86_64_PLT32 ||
        img.rel[0].addend != -4) {
        fprintf(stderr, "call reloc missing\n");
        return 1;
    }
    if (strcmp(img.sym[img.rel[0].sym_index].name, "foo") != 0 ||
        img.sym[img.rel[0].sym_index].binding != CHRISO_BIND_UNDEF) {
        fprintf(stderr, "call target is not an undefined symbol\n");
        return 1;
    }
    if (chrisasm_assemble("push r8\n", &img) != 0 ||
        img.sec_size[CHRISO_SEC_TEXT] < 2u ||
        img.sec[CHRISO_SEC_TEXT][0] != 0x41u ||
        img.sec[CHRISO_SEC_TEXT][1] != 0x50u) {
        fprintf(stderr, "push r8 encoding\n");
        return 1;
    }
    puts("test_chrisasm: ok");
    return 0;
}
