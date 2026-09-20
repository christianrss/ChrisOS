#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"
#include "jit.h"
#include "jit_compile.h"
#include "jit_runtime.h"

static int nosys(ClvmVm *vm, int32_t id, void *user) {
    (void)vm;
    (void)id;
    (void)user;
    return -1;
}

int main(void) {
    static const char *src =
        "void main(){\n"
        " int a;\n"
        " a = 0;\n"
        " while (a < 100) {\n"
        "  a = a + 1;\n"
        " }\n"
        "}\n";
    uint8_t code[4096];
    uint8_t file[4200];
    ChrisResult cr;
    ClvmImage image;
    ClvmVm vm;
    JitBuf jb;
    JitFn fn;
    size_t n;
    ClvmStepResult r;
    uint64_t helpers;

    memset(&vm, 0, sizeof(vm));
    memset(&jb, 0, sizeof(jb));
    memset(&cr, 0, sizeof(cr));
    assert(chrisc_compile(src, strlen(src), code, sizeof(code), &cr));
    n = clvm_write_image_v2(file, sizeof(file), 0, cr.entry, 0, code, cr.code_size);
    assert(n);
    assert(clvm_parse(file, n, &image) == CL_LOAD_OK);
    clvm_vm_init(&vm, &image, nosys, 0);
    assert(jit_compile_image(&image, &jb, &fn) == 0);
    jit_rt_reset_stats();
    r = fn(&vm, 200000, 0);
    helpers = jit_rt_helper_calls();
    if (r != CLVM_STEP_HALT) {
        fprintf(stderr, "jit native halt fail %d fault %d helpers %llu\n",
                (int)r, (int)vm.fault, (unsigned long long)helpers);
        return 1;
    }
    if (helpers != 0) {
        fprintf(stderr, "jit native expected 0 helpers got %llu\n",
                (unsigned long long)helpers);
        return 1;
    }
    printf("test_jit_native helpers=%llu ok\n", (unsigned long long)helpers);
    jit_free(&jb);

    {
        static const char *fs =
            "void main(){\n"
            " float a;\n"
            " float b;\n"
            " float c;\n"
            " a = 1.5;\n"
            " b = 2.0;\n"
            " c = a + b;\n"
            " c = c * b;\n"
            "}\n";
        memset(&vm, 0, sizeof(vm));
        memset(&jb, 0, sizeof(jb));
        memset(&cr, 0, sizeof(cr));
        if (!chrisc_compile(fs, strlen(fs), code, sizeof(code), &cr)) {
            fprintf(stderr, "jit float compile fail\n");
            return 1;
        }
        n = clvm_write_image_v2(file, sizeof(file), 0, cr.entry, 0, code, cr.code_size);
        assert(n);
        assert(clvm_parse(file, n, &image) == CL_LOAD_OK);
        clvm_vm_init(&vm, &image, nosys, 0);
        if (jit_compile_image(&image, &jb, &fn) != 0) {
            fprintf(stderr, "jit float compile image fail\n");
            return 1;
        }
        jit_rt_reset_stats();
        r = fn(&vm, 200000, 0);
        helpers = jit_rt_helper_calls();
        if (r != CLVM_STEP_HALT) {
            fprintf(stderr, "jit float halt fail %d fault %d helpers %llu\n",
                    (int)r, (int)vm.fault, (unsigned long long)helpers);
            return 1;
        }
        if (helpers != 0) {
            fprintf(stderr, "jit float expected 0 helpers got %llu\n",
                    (unsigned long long)helpers);
            return 1;
        }
        jit_free(&jb);
    }
    return 0;
}
