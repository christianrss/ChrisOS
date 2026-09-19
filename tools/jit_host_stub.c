#include <stddef.h>
#include "jit.h"

int jit_emit(JitBuf *buf, const uint8_t *bytes, uint32_t n) {
    uint32_t i;

    if (buf == NULL || bytes == NULL || buf->used + n > buf->cap) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        buf->w[buf->used++] = bytes[i];
    }
    return 0;
}

int jit_alloc(JitBuf *buf) {
    (void)buf;
    return 0;
}

void jit_seal(JitBuf *buf) {
    (void)buf;
}

void jit_free(JitBuf *buf) {
    (void)buf;
}
