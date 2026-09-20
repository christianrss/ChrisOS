#include "clasm.h"
#include "clvm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char src[] =
        "main:\n"
        "PUSH 1\n"
        "PUSH 6\n"
        "SYS\n"
        "HALT\n";
    static const char srcb[] =
        "main:\n"
        "PUSH 65\n"
        "PUSH 0\n"
        "STOREB\n"
        "PUSH 0\n"
        "LOADB\n"
        "HALT\n";
    uint8_t code[256];
    ClasmResult res;

    memset(&res, 0, sizeof(res));
    assert(clasm_compile(src, strlen(src), code, sizeof(code), &res));
    assert(res.code_size > 0);
    assert(code[res.entry] == CL_OP_PUSH);
    memset(&res, 0, sizeof(res));
    assert(clasm_compile(srcb, strlen(srcb), code, sizeof(code), &res));
    assert(res.code_size > 0);
    puts("test_clasm: ok");
    return 0;
}
