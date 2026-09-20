#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static const char src[] =
    "void main(){\n"
    " float a;\n"
    " float b;\n"
    " float c;\n"
    " a=1.5;\n"
    " b=2.0;\n"
    " c=a*b;\n"
    "}\n";

static int fbits(float f) {
    union {
        float fv;
        uint32_t u;
    } v;
    v.fv = f;
    return (int32_t)v.u;
}

int main(void) {
    uint8_t code[4096];
    ChrisResult res;
    ClvmVm vm;
    ClvmImage img;
    ClvmStepResult step;

    memset(&res, 0, sizeof(res));
    assert(chrisc_compile(src, strlen(src), code, sizeof(code), &res));
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.code = code;
    clvm_vm_init(&vm, &img, 0, 0);
    step = clvm_step(&vm, 100000);
    assert(step == CLVM_STEP_HALT || step == CLVM_STEP_YIELD);
    {
        uint32_t got = (uint32_t)vm.memory[8] |
                       ((uint32_t)vm.memory[9] << 8) |
                       ((uint32_t)vm.memory[10] << 16) |
                       ((uint32_t)vm.memory[11] << 24);
        assert(got == (uint32_t)fbits(3.0f));
    }
    puts("test_chrisc_float: ok");
    return 0;
}
