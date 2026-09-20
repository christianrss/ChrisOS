#ifndef CHRIS_JIT_RUNTIME_H
#define CHRIS_JIT_RUNTIME_H

#include <stdint.h>
#include "clvm/clvm.h"
#include "clvm/clvm_vm.h"

int jit_rt_fetch_u32(ClvmVm *vm, uint32_t *out);
int jit_rt_fetch_i16(ClvmVm *vm, int16_t *out);
int jit_rt_push(ClvmVm *vm, int32_t v);
int jit_rt_pop(ClvmVm *vm, int32_t *out);
int jit_rt_binop(ClvmVm *vm, uint8_t op);
int jit_rt_sys(ClvmVm *vm);
int jit_rt_exec(ClvmVm *vm, uint8_t op);
int jit_rt_exec_at_pc(ClvmVm *vm);
int jit_rt_run_range(ClvmVm *vm, uint32_t start, uint32_t end);
void jit_rt_reset_stats(void);
uint64_t jit_rt_helper_calls(void);

#endif
