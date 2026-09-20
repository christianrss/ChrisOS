#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static const char src[] =
    "struct Vec3 { float x; float y; float z; };\n"
    "void main(){\n"
    " struct Vec3 p;\n"
    " struct Vec3 pts[2];\n"
    " p.x=1.0;\n"
    " p.y=2.0;\n"
    " p.z=3.0;\n"
    " pts[1].x=9.0;\n"
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
    assert(rd_u32(vm.memory, 0) == fbits(1.0f));
    assert(rd_u32(vm.memory, 4) == fbits(2.0f));
    assert(rd_u32(vm.memory, 8) == fbits(3.0f));
    assert(rd_u32(vm.memory, 12 + 12) == fbits(9.0f));
    puts("test_chrisc_struct: ok");
    return 0;
}
