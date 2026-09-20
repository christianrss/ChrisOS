#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static const char src[] =
    "float len2(float x, float y){\n"
    " return x*x+y*y;\n"
    "}\n"
    "int add1(int n){\n"
    " return n+1;\n"
    "}\n"
    "void main(){\n"
    " float r;\n"
    " int k;\n"
    " r=len2(3.0,4.0);\n"
    " k=add1(41);\n"
    "}\n";

static uint32_t fbits(float f) {
    union {
        float fv;
        uint32_t u;
    } v;
    v.fv = f;
    return v.u;
}

static uint32_t rd_u32(const uint8_t *m, uint32_t off) {
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
           ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

int main(void) {
    uint8_t code[4096];
    ChrisResult res;
    ClvmVm vm;
    ClvmImage img;
    ClvmStepResult step;
    uint32_t got;
    int found;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "compile failed: %d:%d %s\n", res.diag.line, res.diag.column,
                res.diag.message);
        return 1;
    }
    img.version = CLVM_VERSION;
    img.flags = 0;
    img.entry = res.entry;
    img.code_size = res.code_size;
    img.checksum = 0;
    img.code = code;
    clvm_vm_init(&vm, &img, 0, 0);
    step = clvm_step(&vm, 100000);
    assert(step == CLVM_STEP_HALT || step == CLVM_STEP_YIELD);
    found = 0;
    got = rd_u32(vm.memory, 12);
    if (got == fbits(25.0f))
        found = 1;
    got = rd_u32(vm.memory, 8);
    if (got == fbits(25.0f))
        found = 1;
    got = rd_u32(vm.memory, 16);
    if (got == fbits(25.0f))
        found = 1;
    got = rd_u32(vm.memory, 24);
    if (got == fbits(25.0f))
        found = 1;
    assert(found);
    found = 0;
    if (vm.memory[8] == 42 || vm.memory[12] == 42 || vm.memory[16] == 42 ||
        vm.memory[20] == 42 || vm.memory[24] == 42)
        found = 1;
    assert(found);
    puts("test_chrisc_fn: ok");
    return 0;
}
