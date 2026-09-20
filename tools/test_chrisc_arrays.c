#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static const char src[] =
    "void main(){\n"
    " int v[4];\n"
    " v[0]=10;\n"
    " v[1]=20;\n"
    " int a;\n"
    " a=v[0]+v[1];\n"
    "}\n";

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
    assert(vm.memory[16] == 30 || vm.memory[12] == 30);
    puts("test_chrisc_arrays: ok");
    return 0;
}
