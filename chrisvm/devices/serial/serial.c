#include "machine/machine.h"

#include <string.h>

static int dlab(const ChrisSerial *s) {
    return (s->lcr & 0x80u) != 0;
}

static int serial_in(void *ctx, uint16_t port, int size, uint32_t *value) {
    ChrisSerial *s = (ChrisSerial *)ctx;
    uint16_t reg = (uint16_t)(port & 7u);
    uint32_t v = 0;
    (void)size;
    if (reg == 0) {
        if (dlab(s)) {
            v = s->dll;
        } else if ((s->mcr & 0x10u) != 0) {
            v = s->loop_data;
            s->loop_dr = 0;
        }
    } else if (reg == 1) {
        v = dlab(s) ? s->dlm : s->ier;
    } else if (reg == 2) {
        v = 0x01;
    } else if (reg == 3) {
        v = s->lcr;
    } else if (reg == 4) {
        v = s->mcr;
    } else if (reg == 5) {
        v = 0x60u;
        if (s->loop_dr) {
            v |= 0x01u;
        }
    } else if (reg == 6) {
        v = 0;
    } else {
        v = s->scr;
    }
    *value = v;
    return 0;
}

static int serial_out(void *ctx, uint16_t port, int size, uint32_t value) {
    ChrisSerial *s = (ChrisSerial *)ctx;
    uint16_t reg = (uint16_t)(port & 7u);
    uint8_t b = (uint8_t)value;
    (void)size;
    if (reg == 0) {
        if (dlab(s)) {
            s->dll = b;
        } else if ((s->mcr & 0x10u) != 0) {
            s->loop_data = b;
            s->loop_dr = 1;
        } else {
            if (s->tx_len + 1 < CHRIS_TX_MAX) {
                s->tx[s->tx_len++] = (char)b;
                s->tx[s->tx_len] = 0;
            }
            if (s->hook) {
                s->hook(s->hook_ctx, (char)b);
            }
        }
    } else if (reg == 1) {
        if (dlab(s)) {
            s->dlm = b;
        } else {
            s->ier = b;
        }
    } else if (reg == 2) {
        /* FCR. Writes are accepted and not queued. */
    } else if (reg == 3) {
        s->lcr = b;
    } else if (reg == 4) {
        s->mcr = b;
    } else if (reg == 7) {
        s->scr = b;
    }
    return 0;
}

static int shutdown_in(void *ctx, uint16_t port, int size, uint32_t *value) {
    (void)ctx;
    (void)port;
    (void)size;
    *value = 0;
    return 0;
}

static int shutdown_out(void *ctx, uint16_t port, int size, uint32_t value) {
    ChrisMachine *m = (ChrisMachine *)ctx;
    (void)port;
    (void)size;
    if ((value & 0xffu) == 0x01u) {
        m->shutdown = 1;
        if (m->cpu) {
            m->cpu->exit_reason = CHRIS_EXIT_SHUTDOWN;
            m->cpu->halted = 1;
        }
    }
    return 0;
}

void chris_serial_attach(ChrisMachine *m) {
    memset(&m->serial, 0, sizeof m->serial);
    chris_io_map(m, 0x3f8, 0x3ff, serial_in, serial_out, &m->serial);
    chris_io_map(m, CHRIS_SHUTDOWN_PORT, CHRIS_SHUTDOWN_PORT, shutdown_in, shutdown_out, m);
}
