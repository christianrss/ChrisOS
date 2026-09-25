#include "machine/machine.h"

#include <string.h>

int chris_io_map(ChrisMachine *m, uint16_t start, uint16_t end,
                 int (*in)(void *, uint16_t, int, uint32_t *),
                 int (*out)(void *, uint16_t, int, uint32_t), void *ctx) {
    int i;
    if (!m || start > end) {
        return -1;
    }
    for (i = 0; i < CHRIS_IO_MAX; ++i) {
        if (!m->io[i].used) {
            m->io[i].used = 1;
            m->io[i].start = start;
            m->io[i].end = end;
            m->io[i].in = in;
            m->io[i].out = out;
            m->io[i].ctx = ctx;
            return 0;
        }
    }
    return -1;
}

static ChrisIoSlot *find_io(ChrisMachine *m, uint16_t port) {
    int i;
    for (i = 0; i < CHRIS_IO_MAX; ++i) {
        if (m->io[i].used && port >= m->io[i].start && port <= m->io[i].end) {
            return &m->io[i];
        }
    }
    return 0;
}

int chris_io_in(ChrisMachine *m, uint16_t port, int size, uint32_t *value) {
    ChrisIoSlot *s;
    if (!m || !value || (size != 1 && size != 2 && size != 4)) {
        return -1;
    }
    s = find_io(m, port);
    if (!s) {
        *value = size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu;
        return 0;
    }
    return s->in(s->ctx, port, size, value);
}

int chris_io_out(ChrisMachine *m, uint16_t port, int size, uint32_t value) {
    ChrisIoSlot *s;
    if (!m || (size != 1 && size != 2 && size != 4)) {
        return -1;
    }
    s = find_io(m, port);
    if (!s) {
        return 0;
    }
    return s->out(s->ctx, port, size, value);
}
