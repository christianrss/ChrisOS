#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../compiler/clvm/clasm.h"
#include "../compiler/clvm/clvm.h"

int main(void) {
    static const char source[] =
        "main:\n"
        "  PUSH 3\n"
        "loop: DUP\n"
        "  PUSH 1\n"
        "  SUB\n"
        "  DUP\n"
        "  JNZ loop\n"
        "  DROP\n"
        "  HALT\n";
    uint8_t code[256], file[272];
    ClasmResult r;
    ClvmImage image;
    size_t n;
    assert(clasm_compile(source, strlen(source), code, sizeof(code), &r));
    assert(r.entry == 0 && r.code_size > 0);
    n = clvm_write_image(file, sizeof(file), CLVM_FLAG_GAME,
                         r.entry, code, r.code_size);
    assert(n != 0);
    assert(clvm_parse(file, n, &image) == CL_LOAD_OK);
    assert(!clasm_compile("JMP missing\n", 12, code, sizeof(code), &r));
    assert(strcmp(r.diag.message, "undefined label") == 0);
    puts("test_clasm: ok");
    return 0;
}
