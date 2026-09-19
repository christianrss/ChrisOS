#include "jit_compile.h"

#include "jit_emit.h"

extern ClvmStepResult clvm_step(ClvmVm *vm, uint32_t budget);

int jit_compile_image(const ClvmImage *image, JitBuf *buf, JitFn *fn_out) {
    (void)image;

    if (buf == NULL || fn_out == NULL) {
        return -1;
    }
    if (jit_alloc(buf) != 0) {
        return -1;
    }
    if (jit_emit_prologue(buf, 0) != 0 ||
        jit_emit_call_r64(buf, (uint64_t)(uintptr_t)clvm_step) != 0 ||
        jit_emit_epilogue(buf) != 0) {
        jit_free(buf);
        return -1;
    }
    jit_seal(buf);
    *fn_out = (JitFn)(uintptr_t)buf->x;
    return 0;
}
