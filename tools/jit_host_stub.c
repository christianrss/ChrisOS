#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#include "jit.h"

/* Stubs for kernel serial / GC pulled in by jit_compile.c */
#ifndef JIT_HOST_EXTERNAL_SERIAL
void serial_puts(const char *text) { (void)text; }
void serial_write_u64(uint64_t value) { (void)value; }
void serial_write_hex(uint64_t value) { (void)value; }
#endif
void gc_poll(void) {}

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
    page = mmap(NULL, JIT_MAX, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return -1;
    }
    buf->phys = 1;
    buf->w = (uint8_t *)page;
    buf->x = (uint8_t *)page;
    buf->used = 0;
    buf->cap = JIT_MAX;
    buf->pages = JIT_PAGES;
    return 0;
}

void jit_seal(JitBuf *buf) {
    if (buf == NULL || buf->x == NULL) {
        return;
    }
    mprotect(buf->x, JIT_MAX, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void jit_free(JitBuf *buf) {
    if (buf == NULL || buf->w == NULL) {
        return;
    }
    munmap(buf->w, JIT_MAX);
    buf->w = NULL;
    buf->x = NULL;
    buf->used = 0;
    buf->cap = 0;
}
