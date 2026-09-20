#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static int sys_nop(ClvmVm *vm, int32_t id, void *user) {
    (void)vm;
    (void)id;
    (void)user;
    return -1;
}

int main(void) {
    static const char src[] =
        "void main(){\n"
        " char buf[8];\n"
        " int p;\n"
        " int i;\n"
        " int ok;\n"
        " p=\"hi\";\n"
        " buf[0]=65;\n"
        " buf[1]=0;\n"
        " i=0;\n"
        " ok=0;\n"
        " while(1){\n"
        "  if(i==3){ break; }\n"
        "  i=i+1;\n"
        "  continue;\n"
        " }\n"
        " if(1 && 0){ ok=99; }\n"
        " if(0 || 1){ ok=ok+1; }\n"
        " if(!0){ ok=ok+1; }\n"
        " storeb(p+2, 33);\n"
        "}\n";
    uint8_t code[4096];
    ChrisResult res;
    ClvmImage img;
    ClvmVm vm;
    ClvmStepResult st;

    memset(&res, 0, sizeof(res));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &res)) {
        fprintf(stderr, "%d:%d %s\n", res.diag.line, res.diag.column,
                res.diag.message);
        return 1;
    }
    assert(res.code_size > 0);
    memset(&img, 0, sizeof(img));
    img.code = code;
    img.code_size = (uint32_t)res.code_size;
    img.entry = res.entry;
    clvm_vm_init(&vm, &img, sys_nop, 0);
    st = clvm_step(&vm, 100000u);
    assert(st == CLVM_STEP_HALT);
    assert(vm.fault == CLVM_FAULT_NONE);
    puts("test_chrisc_string: ok");
    return 0;
}
