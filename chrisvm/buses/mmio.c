#include "machine/machine.h"

#include <string.h>

int chris_mmio_map(ChrisMachine *m, uint64_t base, uint64_t size,
                   int (*read)(void *, uint64_t, int, uint64_t *),
                   int (*write)(void *, uint64_t, int, uint64_t), void *ctx) {
    int i;
    if (!m || size == 0) {
        return -1;
    }
    for (i = 0; i < CHRIS_MMIO_MAX; ++i) {
        if (!m->mmio[i].used) {
            m->mmio[i].used = 1;
            m->mmio[i].base = base;
            m->mmio[i].size = size;
            m->mmio[i].read = read;
            m->mmio[i].write = write;
            m->mmio[i].ctx = ctx;
            return 0;
        }
    }
    return -1;
}

int chris_mmio_find(ChrisMachine *m, uint64_t pa, uint64_t *offset) {
    int i;
    if (!m) {
        return -1;
    }
    for (i = 0; i < CHRIS_MMIO_MAX; ++i) {
        if (!m->mmio[i].used) {
            continue;
        }
        if (pa >= m->mmio[i].base && pa - m->mmio[i].base < m->mmio[i].size) {
            if (offset) {
                *offset = pa - m->mmio[i].base;
            }
            return i;
        }
    }
    return -1;
}

int chris_phys_read(ChrisMachine *m, uint64_t pa, void *dst, size_t n) {
    uint8_t *out = (uint8_t *)dst;
    size_t i;
    if (!m || !dst) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (pa < m->ram_size && n <= m->ram_size - pa) {
        memcpy(out, m->ram + pa, n);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        int slot;
        uint64_t off = 0;
        uint64_t value = 0;
        slot = chris_mmio_find(m, pa + i, &off);
        if (slot < 0) {
            return -1;
        }
        if (m->mmio[slot].read(m->mmio[slot].ctx, off, 1, &value) != 0) {
            return -1;
        }
        out[i] = (uint8_t)value;
    }
    return 0;
}

int chris_phys_write(ChrisMachine *m, uint64_t pa, const void *src, size_t n) {
    const uint8_t *in = (const uint8_t *)src;
    size_t i;
    if (!m || (!src && n)) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (pa < m->ram_size && n <= m->ram_size - pa) {
        memcpy(m->ram + pa, in, n);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        int slot;
        uint64_t off = 0;
        slot = chris_mmio_find(m, pa + i, &off);
        if (slot < 0) {
            return -1;
        }
        if (m->mmio[slot].write(m->mmio[slot].ctx, off, 1, in[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
