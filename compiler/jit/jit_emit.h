#ifndef CHRIS_JIT_EMIT_H
#define CHRIS_JIT_EMIT_H

#include <stdint.h>
#include "jit.h"

int jit_emit_u8(JitBuf *j, uint8_t b);
int jit_emit_u32(JitBuf *j, uint32_t v);
int jit_emit_u64(JitBuf *j, uint64_t v);

int jit_emit_ret(JitBuf *j);
int jit_emit_mov_r32_imm(JitBuf *j, int reg, int32_t v);
int jit_emit_mov_r64_imm(JitBuf *j, int reg, uint64_t v);
int jit_emit_mov_r64_r64(JitBuf *j, int dst, int src);
int jit_emit_add_r32_r32(JitBuf *j, int dst, int src);
int jit_emit_sub_r32_r32(JitBuf *j, int dst, int src);
int jit_emit_imul_r32_r32(JitBuf *j, int dst, int src);
int jit_emit_cmp_r32_r32(JitBuf *j, int a, int b);
int jit_emit_cmp_r32_imm8(JitBuf *j, int reg, int8_t imm);
int jit_emit_test_r32_r32(JitBuf *j, int a, int b);
int jit_emit_je_rel32(JitBuf *j, int32_t rel);
int jit_emit_jne_rel32(JitBuf *j, int32_t rel);
int jit_emit_jge_rel32(JitBuf *j, int32_t rel);
int jit_emit_jmp_rel32(JitBuf *j, int32_t rel);
int jit_emit_call_r64(JitBuf *j, uint64_t target);
int jit_emit_prologue(JitBuf *j, int locals);
int jit_emit_epilogue(JitBuf *j);

#endif
