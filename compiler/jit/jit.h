#ifndef CHRIS_JIT_H
#define CHRIS_JIT_H

#include <stdint.h>

#define JIT_PAGES 256u
#define JIT_MAX (JIT_PAGES * 4096u)

typedef struct JitBuf {
    uint64_t phys;
    uint8_t *w;
    uint8_t *x;
    uint32_t used;
    uint32_t cap;
    uint32_t pages;
} JitBuf;

int jit_alloc(JitBuf *buf);
int jit_emit(JitBuf *buf, const uint8_t *bytes, uint32_t n);
void jit_seal(JitBuf *buf);
void jit_free(JitBuf *buf);

struct ClvmVm;
int jit_sys_trampoline(int32_t id);
void jit_set_sys_context(struct ClvmVm *vm, void *user);

#endif
