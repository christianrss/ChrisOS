#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#include "jit.h"

int jit_emit(JitBuf *buf, const uint8_t *bytes, uint32_t n) {
    uint32_t i;

    if (buf == NULL || bytes == NULL || buf->w == NULL || buf->used + n > buf->cap) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        buf->w[buf->used++] = bytes[i];
    }
    return 0;
}

int jit_alloc(JitBuf *buf) {
    void *page;

    if (buf == NULL) {
        return -1;
    }
    page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return -1;
    }
    buf->phys = 0;
    buf->w = (uint8_t *)page;
    buf->x = (uint8_t *)page;
    buf->used = 0;
    buf->cap = 4096;
    return 0;
}

void jit_seal(JitBuf *buf) {
    if (buf == NULL || buf->x == NULL) {
        return;
    }
    mprotect(buf->x, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void jit_free(JitBuf *buf) {
    if (buf == NULL || buf->w == NULL) {
        return;
    }
    munmap(buf->w, 4096);
    buf->w = NULL;
    buf->x = NULL;
    buf->used = 0;
    buf->cap = 0;
}
