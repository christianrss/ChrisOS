#ifndef CHRIS_JIT_COMPILE_H
#define CHRIS_JIT_COMPILE_H

#include <stdint.h>
#include "clvm/clvm.h"
#include "clvm/clvm_vm.h"
#include "jit.h"

typedef ClvmStepResult (*JitFn)(ClvmVm *vm, uint32_t budget, uint32_t now);

int jit_compile_image(const ClvmImage *image, JitBuf *buf, JitFn *fn_out);

#endif
