/* Poll-only xHCI for one qemu-xhci keyboard and one boot mouse.
 * UHCI MSC stays in usb_msc.c. See docs/XHCI.md. */
#include "xhci.h"

#include "hwgate.h"
#include "input.h"
#include "pci.h"
#include "serial.h"

#define TRB_CYCLE 1u
#define TRB_ISP (1u << 2)
#define TRB_CH (1u << 4)
#define TRB_IOC (1u << 5)
#define TRB_IDT (1u << 6)
#define TRB_TYPE(t) ((uint32_t)(t) << 10)
#define TRB_LINK 6u
#define TRB_SETUP 2u
#define TRB_DATA 3u
#define TRB_STATUS 4u
#define TRB_NORMAL 1u
#define TRB_ENABLE 9u
#define TRB_ADDR 11u
#define TRB_CONFIG 12u
#define EVT_TRANSFER 32u
#define EVT_COMPLETE 33u

#define OFF_CMD 0x0000u
#define OFF_EVT 0x1000u
#define OFF_ERST 0x2000u
#define OFF_DCBA 0x2100u
#define OFF_DESC 0x3000u
#define DEV_BASE 0x4000u
#define DEV_STRIDE 0x4000u
#define RING_N 63
#define EVT_N 64
#define DMA_PAGES 12

typedef struct Ring {
    uint32_t off;
    int index;
    int cycle;
} Ring;

typedef struct HidDev {
    int live;
    int kind; /* 1 keyboard, 2 mouse */
    uint8_t slot;
    uint8_t port;
    uint8_t iface;
    uint8_t ep;
    uint8_t dci;
    uint8_t interval;
    uint16_t mps;
    uint8_t prev_mod;
    uint8_t prev_key[6];
    Ring ep0;
    Ring intr;
    uint32_t in_off;
    uint32_t out_off;
    uint32_t data_off;
} HidDev;

static int g_dma = -1;
static int g_win = -1;
static uint32_t g_op;
static uint32_t g_db;
static uint32_t g_rt;
static uint32_t g_ctx;
static int g_ports;
static Ring g_cmd;
static int g_evt_i;
static int g_evt_cycle;
static HidDev g_dev[2];
static int g_ready;
static int g_kbd;
static int g_mouse;

static uint32_t rd(uint32_t off) {
    return hw_mmio_r32(g_win, off);
}

static void wr(uint32_t off, uint32_t val) {
    (void)hw_mmio_w32(g_win, off, val);
}

static uint32_t dma_r32(uint32_t off) {
    return hw_dma_r32(g_dma, off);
}

static void dma_w32(uint32_t off, uint32_t val) {
    (void)hw_dma_w32(g_dma, off, val);
}

static uint8_t dma_r8(uint32_t off) {
    uint32_t w = dma_r32(off & ~3u);
    return (uint8_t)(w >> ((off & 3u) * 8u));
}

static void phys_of(uint32_t off, uint32_t *lo, uint32_t *hi) {
    uint64_t p = ((uint64_t)hw_dma_hi(g_dma) << 32) | hw_dma_lo(g_dma);
    p += off;
    *lo = (uint32_t)p;
    *hi = (uint32_t)(p >> 32);
}

static void trb_write(uint32_t at, uint32_t lo, uint32_t hi, uint32_t st,
                      uint32_t ctrl) {
    dma_w32(at, lo);
    dma_w32(at + 4u, hi);
    dma_w32(at + 8u, st);
    dma_w32(at + 12u, ctrl);
}

static void ring_init(Ring *r, uint32_t off) {
    uint32_t lo, hi;
    uint32_t i;
    r->off = off;
    r->index = 0;
    r->cycle = 1;
    for (i = 0; i < (RING_N + 1u) * 16u; i += 4u)
        dma_w32(off + i, 0);
    phys_of(off, &lo, &hi);
    trb_write(off + RING_N * 16u, lo, hi, 0, TRB_CYCLE | TRB_TYPE(TRB_LINK) | (1u << 1));
}

static void ring_push(Ring *r, uint32_t lo, uint32_t hi, uint32_t st, uint32_t ctrl) {
    uint32_t plo, phi;
    if (r->index == RING_N) {
        phys_of(r->off, &plo, &phi);
        trb_write(r->off + RING_N * 16u, plo, phi, 0,
                  (r->cycle ? TRB_CYCLE : 0u) | TRB_TYPE(TRB_LINK) | (1u << 1));
        r->index = 0;
        r->cycle ^= 1;
    }
    if (r->cycle)
        ctrl |= TRB_CYCLE;
    else
        ctrl &= ~TRB_CYCLE;
    trb_write(r->off + (uint32_t)r->index * 16u, lo, hi, st, ctrl);
    r->index++;
}

static int event_take(uint32_t out[4]) {
    uint32_t at = OFF_EVT + (uint32_t)g_evt_i * 16u;
    uint32_t ctrl = dma_r32(at + 12u);
    uint32_t lo, hi;
    int bit = (int)(ctrl & 1u);
    if (bit != g_evt_cycle)
        return 0;
    out[0] = dma_r32(at);
    out[1] = dma_r32(at + 4u);
    out[2] = dma_r32(at + 8u);
    out[3] = ctrl;
    g_evt_i++;
    if (g_evt_i == EVT_N) {
        g_evt_i = 0;
        g_evt_cycle ^= 1;
    }
    phys_of(OFF_EVT + (uint32_t)g_evt_i * 16u, &lo, &hi);
    wr(g_rt + 0x18u, lo | 8u);
    wr(g_rt + 0x1Cu, hi);
    return 1;
}

static int event_wait(uint32_t out[4]) {
    int spins = 0;
    for (;;) {
        if (event_take(out))
            return 0;
        (void)rd(g_op + 4u);
        if ((spins & 4095) == 0)
            serial_putc(0);
        if (++spins > 400000)
            return -1;
        __asm__ volatile ("pause");
    }
}

static int cmd_wait(void) {
    uint32_t ev[4];
    int spins = 0;
    for (;;) {
        if (event_take(ev)) {
            uint32_t type = (ev[3] >> 10) & 0x3Fu;
            uint32_t code = ev[2] >> 24;
            if (type == EVT_COMPLETE)
                return code == 1u ? 0 : -1;
        }
        (void)rd(g_op + 4u);
        if ((spins & 4095) == 0)
            serial_putc(0);
        if (++spins > 400000)
            return -1;
        __asm__ volatile ("pause");
    }
}

static void cmd_ring(void) {
    wr(g_db, 0);
}

static int cmd_slot(uint8_t *slot) {
    uint32_t ev[4];
    int spins = 0;
    ring_push(&g_cmd, 0, 0, 0, TRB_TYPE(TRB_ENABLE));
    cmd_ring();
    for (;;) {
        if (event_take(ev)) {
            uint32_t type = (ev[3] >> 10) & 0x3Fu;
            if (type == EVT_COMPLETE) {
                if ((ev[2] >> 24) != 1u)
                    return -1;
                *slot = (uint8_t)(ev[3] >> 24);
                return *slot ? 0 : -1;
            }
        }
        (void)rd(g_op + 4u);
        if ((spins & 4095) == 0)
            serial_putc(0);
        if (++spins > 400000)
            return -1;
        __asm__ volatile ("pause");
    }
}

static int ctrl(HidDev *d, uint8_t bm, uint8_t br, uint16_t val, uint16_t idx,
                uint16_t len, int dir_in) {
    uint32_t lo, hi;
    uint32_t setup_lo = (uint32_t)bm | ((uint32_t)br << 8) | ((uint32_t)val << 16);
    uint32_t setup_hi = (uint32_t)idx | ((uint32_t)len << 16);
    uint32_t trt = len ? (dir_in ? 3u : 2u) : 0u;
    uint32_t ev[4];
    ring_push(&d->ep0, setup_lo, setup_hi, 8u,
              TRB_IDT | TRB_CH | TRB_TYPE(TRB_SETUP) | (trt << 16));
    if (len) {
        phys_of(OFF_DESC, &lo, &hi);
        ring_push(&d->ep0, lo, hi, len,
                  TRB_CH | TRB_TYPE(TRB_DATA) | (dir_in ? (1u << 16) : 0u));
    }
    ring_push(&d->ep0, 0, 0, 0,
              TRB_IOC | TRB_TYPE(TRB_STATUS) | ((len && dir_in) ? 0u : (1u << 16)));
    wr(g_db + (uint32_t)d->slot * 4u, 1u);
    if (event_wait(ev) != 0)
        return -1;
    if (((ev[3] >> 10) & 0x3Fu) != EVT_TRANSFER)
        return -1;
    {
        uint32_t code = ev[2] >> 24;
        if (code != 1u && code != 13u)
            return -1;
    }
    return 0;
}

static void ctx_dw(uint32_t base, uint32_t index, uint32_t dw, uint32_t val) {
    dma_w32(base + index * g_ctx + dw * 4u, val);
}

static void ep_ctx(uint32_t base, uint32_t index, uint32_t type, uint16_t mps,
                   uint8_t interval, uint32_t ring_off) {
    uint32_t lo, hi;
    phys_of(ring_off, &lo, &hi);
    ctx_dw(base, index, 0, (uint32_t)interval << 16);
    ctx_dw(base, index, 1, (3u << 1) | (type << 3) | ((uint32_t)mps << 16));
    ctx_dw(base, index, 2, lo | 1u);
    ctx_dw(base, index, 3, hi);
    ctx_dw(base, index, 4, mps ? mps : 8u);
}

static int address_dev(HidDev *d, uint32_t speed, uint16_t mps) {
    uint32_t lo, hi;
    uint32_t i;
    uint32_t slot_dw0 = (speed << 20) | (1u << 27);
    uint32_t slot_dw1 = (uint32_t)d->port << 16;
    for (i = 0; i < 33u * g_ctx; i += 4u)
        dma_w32(d->in_off + i, 0);
    for (i = 0; i < 32u * g_ctx; i += 4u)
        dma_w32(d->out_off + i, 0);
    ctx_dw(d->in_off, 0, 1, (1u << 0) | (1u << 1));
    ctx_dw(d->in_off, 1, 0, slot_dw0);
    ctx_dw(d->in_off, 1, 1, slot_dw1);
    ep_ctx(d->in_off, 2, 4u, mps, 0, d->ep0.off);
    phys_of(d->out_off, &lo, &hi);
    dma_w32(OFF_DCBA + (uint32_t)d->slot * 8u, lo);
    dma_w32(OFF_DCBA + (uint32_t)d->slot * 8u + 4u, hi);
    phys_of(d->in_off, &lo, &hi);
    ring_push(&g_cmd, lo, hi, 0, TRB_TYPE(TRB_ADDR) | ((uint32_t)d->slot << 24));
    cmd_ring();
    return cmd_wait();
}

static int config_ep(HidDev *d, uint32_t speed) {
    uint32_t lo, hi;
    uint32_t i;
    uint32_t dci = d->dci;
    for (i = 0; i < 33u * g_ctx; i += 4u)
        dma_w32(d->in_off + i, 0);
    ctx_dw(d->in_off, 0, 1, (1u << 0) | (1u << dci));
    ctx_dw(d->in_off, 1, 0, (speed << 20) | (dci << 27));
    ctx_dw(d->in_off, 1, 1, (uint32_t)d->port << 16);
    ep_ctx(d->in_off, dci + 1u, 7u, d->mps, d->interval, d->intr.off);
    phys_of(d->in_off, &lo, &hi);
    ring_push(&g_cmd, lo, hi, 0, TRB_TYPE(TRB_CONFIG) | ((uint32_t)d->slot << 24));
    cmd_ring();
    return cmd_wait();
}

static void arm_intr(HidDev *d) {
    uint32_t lo, hi;
    uint32_t n = d->mps ? d->mps : 8u;
    if (n > 8u)
        n = 8u;
    phys_of(d->data_off, &lo, &hi);
    ring_push(&d->intr, lo, hi, n, TRB_IOC | TRB_ISP | TRB_TYPE(TRB_NORMAL));
    wr(g_db + (uint32_t)d->slot * 4u, d->dci);
}

static int port_reset(int port, uint32_t *speed) {
    uint32_t off = g_op + 0x400u + (uint32_t)(port - 1) * 0x10u;
    uint32_t v;
    int spins;
    v = rd(off);
    if ((v & 1u) == 0)
        return -1;
    v &= ~((1u << 1) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) |
           (1u << 21) | (1u << 22) | (1u << 23));
    v |= (1u << 4) | (1u << 9);
    wr(off, v);
    for (spins = 0; spins < 400000; spins++) {
        v = rd(off);
        if ((v & (1u << 4)) == 0 && (v & (1u << 1)) != 0) {
            *speed = (v >> 10) & 0xFu;
            wr(off, v | (1u << 21));
            return 0;
        }
        if ((spins & 4095) == 0)
            serial_putc(0);
        __asm__ volatile ("pause");
    }
    return -1;
}

static const uint8_t g_set1[0x40] = {
    0,    0,    0,    0,    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
    0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,
    0x1B, 0x2B, 0,    0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0,    0,    0,
    0,    0,    0,    0
};

static void key_emit(uint8_t code, int ext, int release) {
    if (ext)
        input_keyboard_irq(0xE0u);
    input_keyboard_irq(release ? (uint8_t)(code | 0x80u) : code);
}

static void key_one(uint8_t usage, int release) {
    uint8_t code = 0;
    int ext = 0;
    if (usage >= 0x4Fu && usage <= 0x52u) {
        static const uint8_t arrows[4] = {0x4Du, 0x4Bu, 0x50u, 0x48u};
        code = arrows[usage - 0x4Fu];
        ext = 1;
    } else if (usage < 0x40u) {
        code = g_set1[usage];
    }
    if (code)
        key_emit(code, ext, release);
}

static void key_mod(uint8_t bit, int release) {
    if (bit == 0x01u)
        key_emit(0x1Du, 0, release);
    else if (bit == 0x02u)
        key_emit(0x2Au, 0, release);
    else if (bit == 0x04u)
        key_emit(0x38u, 0, release);
    else if (bit == 0x10u)
        key_emit(0x1Du, 1, release);
    else if (bit == 0x20u)
        key_emit(0x36u, 0, release);
    else if (bit == 0x40u)
        key_emit(0x38u, 1, release);
}

static int key_held(const uint8_t *keys, uint8_t usage) {
    int i;
    if (!usage)
        return 0;
    for (i = 0; i < 6; i++) {
        if (keys[i] == usage)
            return 1;
    }
    return 0;
}

static void on_keyboard(HidDev *d, const uint8_t *rep) {
    uint8_t mod = rep[0];
    uint8_t bit;
    int i;
    for (bit = 0x01u; bit != 0; bit = (uint8_t)(bit << 1)) {
        int was = (d->prev_mod & bit) != 0;
        int now = (mod & bit) != 0;
        if (now && !was)
            key_mod(bit, 0);
        if (was && !now)
            key_mod(bit, 1);
    }
    for (i = 0; i < 6; i++) {
        if (rep[2 + i] && !key_held(d->prev_key, rep[2 + i]))
            key_one(rep[2 + i], 0);
    }
    for (i = 0; i < 6; i++) {
        if (d->prev_key[i] && !key_held(rep + 2, d->prev_key[i]))
            key_one(d->prev_key[i], 1);
    }
    d->prev_mod = mod;
    for (i = 0; i < 6; i++)
        d->prev_key[i] = rep[2 + i];
}

static void on_report(HidDev *d) {
    uint8_t rep[8];
    uint32_t i;
    uint32_t n = d->mps ? d->mps : 8u;
    if (n > 8u)
        n = 8u;
    for (i = 0; i < n; i++)
        rep[i] = dma_r8(d->data_off + i);
    if (d->kind == 1 && n >= 8u)
        on_keyboard(d, rep);
    else if (d->kind == 2 && n >= 3u)
        input_mouse_add((int)(int8_t)rep[1], (int)(int8_t)rep[2], rep[0]);
}

static int claim_port(int port, uint32_t speed) {
    HidDev *d;
    uint8_t desc[128];
    uint32_t n;
    uint32_t i;
    uint8_t slot = 0;
    uint8_t conf = 1;
    uint8_t iface = 0;
    uint8_t proto = 0;
    uint8_t ep = 0x81u;
    uint16_t mps = 8;
    uint8_t interval = 10;
    uint16_t ep0_mps = (speed == 2u) ? 8u : 64u;
    int slot_i;
    if (speed == 0u)
        speed = 1u;
    if (g_kbd && g_mouse)
        return 0;
    if (cmd_slot(&slot) != 0 || slot == 0 || slot > 8)
        return -1;
    slot_i = g_kbd + g_mouse;
    if (slot_i > 1)
        return -1;
    d = &g_dev[slot_i];
    d->slot = slot;
    d->port = (uint8_t)port;
    d->in_off = DEV_BASE + (uint32_t)slot_i * DEV_STRIDE;
    d->out_off = d->in_off + 0x1000u;
    d->data_off = d->in_off + 0x3800u;
    ring_init(&d->ep0, d->in_off + 0x2000u);
    ring_init(&d->intr, d->in_off + 0x3000u);
    if (address_dev(d, speed, ep0_mps) != 0)
        return -1;
    if (ctrl(d, 0x80u, 6u, 0x0200u, 0, 9u, 1) != 0)
        return -1;
    n = dma_r8(OFF_DESC + 2u);
    if (n < 9u || n > 128u)
        n = 64u;
    if (ctrl(d, 0x80u, 6u, 0x0200u, 0, (uint16_t)n, 1) != 0)
        return -1;
    for (i = 0; i < n && i < 128u; i++)
        desc[i] = dma_r8(OFF_DESC + i);
    conf = desc[5] ? desc[5] : 1u;
    i = 0;
    while (i + 1u < n) {
        uint8_t len = desc[i];
        uint8_t typ = desc[i + 1u];
        if (len < 2u)
            break;
        if (typ == 4u && i + 8u < n && desc[i + 5u] == 3u && desc[i + 6u] == 1u) {
            iface = desc[i + 2u];
            proto = desc[i + 7u];
        }
        if (typ == 5u && i + 6u < n && proto != 0 && (desc[i + 2u] & 0x80u)) {
            ep = desc[i + 2u];
            mps = (uint16_t)desc[i + 4u] | ((uint16_t)desc[i + 5u] << 8);
            interval = desc[i + 6u] ? desc[i + 6u] : 10u;
            break;
        }
        i += len;
    }
    if (proto != 1u && proto != 2u)
        return 0;
    if ((proto == 1u && g_kbd) || (proto == 2u && g_mouse))
        return 0;
    if (ctrl(d, 0x00u, 9u, conf, 0, 0, 0) != 0)
        return -1;
    (void)ctrl(d, 0x21u, 0x0Bu, 0, iface, 0, 0);
    (void)ctrl(d, 0x21u, 0x0Au, 0, iface, 0, 0);
    d->kind = proto;
    d->iface = iface;
    d->ep = ep;
    d->dci = (uint8_t)(((ep & 0x0Fu) * 2u) + 1u);
    d->interval = interval;
    d->mps = mps ? mps : (proto == 1u ? 8u : 4u);
    if (d->dci < 2u || d->dci > 30u)
        return -1;
    if (config_ep(d, speed) != 0)
        return -1;
    d->live = 1;
    if (proto == 1u)
        g_kbd = 1;
    else
        g_mouse = 1;
    return 0;
}

static int reset_hc(void) {
    uint32_t cmd;
    uint32_t sts;
    int spins;
    uint32_t lo, hi;
    uint32_t cap = (uint32_t)hw_mmio_r8(g_win, 0);
    uint32_t hcs1;
    uint32_t hcc;
    uint32_t dboff;
    uint32_t rtsoff;
    if (cap < 0x10u)
        return -1;
    hcs1 = rd(4u);
    hcc = rd(0x10u);
    dboff = rd(0x14u);
    rtsoff = rd(0x18u);
    g_op = cap;
    g_db = dboff;
    g_rt = rtsoff + 0x20u;
    g_ctx = (hcc & 4u) ? 64u : 32u;
    g_ports = (int)((hcs1 >> 24) & 0xFFu);
    if (g_ports < 1 || g_ports > 16)
        g_ports = 8;
    cmd = rd(g_op);
    if ((rd(g_op + 4u) & 1u) == 0) {
        wr(g_op, cmd & ~1u);
        for (spins = 0; spins < 200000; spins++) {
            if (rd(g_op + 4u) & 1u)
                break;
            __asm__ volatile ("pause");
        }
    }
    wr(g_op, 1u << 1);
    for (spins = 0; spins < 400000; spins++) {
        cmd = rd(g_op);
        sts = rd(g_op + 4u);
        if ((cmd & 2u) == 0 && (sts & (1u << 11)) == 0)
            break;
        if ((spins & 4095) == 0)
            serial_putc(0);
        __asm__ volatile ("pause");
    }
    if (rd(g_op) & 2u)
        return -1;
    wr(g_op + 0x38u, 8u);
    phys_of(OFF_DCBA, &lo, &hi);
    wr(g_op + 0x30u, lo);
    wr(g_op + 0x34u, hi);
    ring_init(&g_cmd, OFF_CMD);
    phys_of(OFF_CMD, &lo, &hi);
    wr(g_op + 0x18u, lo | 1u);
    wr(g_op + 0x1Cu, hi);
    phys_of(OFF_EVT, &lo, &hi);
    dma_w32(OFF_ERST, lo);
    dma_w32(OFF_ERST + 4u, hi);
    dma_w32(OFF_ERST + 8u, EVT_N);
    dma_w32(OFF_ERST + 12u, 0);
    wr(g_rt + 0x08u, 1u);
    phys_of(OFF_ERST, &lo, &hi);
    wr(g_rt + 0x10u, lo);
    wr(g_rt + 0x14u, hi);
    phys_of(OFF_EVT, &lo, &hi);
    wr(g_rt + 0x18u, lo);
    wr(g_rt + 0x1Cu, hi);
    g_evt_i = 0;
    g_evt_cycle = 1;
    wr(g_op, 1u);
    for (spins = 0; spins < 200000; spins++) {
        if ((rd(g_op + 4u) & 1u) == 0)
            return 0;
        __asm__ volatile ("pause");
    }
    return -1;
}

int xhci_hid_ready(void) {
    return g_ready;
}

void xhci_hid_poll(void) {
    uint32_t ev[4];
    int n = 0;
    if (!g_ready)
        return;
    while (n < 8 && event_take(ev)) {
        uint32_t type = (ev[3] >> 10) & 0x3Fu;
        uint8_t slot = (uint8_t)(ev[3] >> 24);
        uint32_t code = ev[2] >> 24;
        int i;
        n++;
        if (type != EVT_TRANSFER)
            continue;
        for (i = 0; i < 2; i++) {
            if (!g_dev[i].live || g_dev[i].slot != slot)
                continue;
            if (code == 1u || code == 13u)
                on_report(&g_dev[i]);
            arm_intr(&g_dev[i]);
        }
    }
}

int xhci_hid_probe(void) {
    uint8_t bus, slot, func;
    int port;
    g_ready = 0;
    g_kbd = 0;
    g_mouse = 0;
    for (bus = 0; bus < 1; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (func = 0; func < 8; func++) {
                uint32_t id = pci_read(bus, slot, func, 0);
                uint32_t cls;
                uint32_t cmd;
                if ((id & 0xFFFFu) == 0xFFFFu)
                    break;
                cls = pci_read(bus, slot, func, 8);
                if ((cls >> 8) != 0x0C0330u)
                    continue;
                g_win = hw_bar_map(bus, slot, func, 0);
                if (g_win < 0)
                    return 0;
                cmd = pci_read(bus, slot, func, 4);
                pci_write(bus, slot, func, 4, cmd | 0x06u);
                g_dma = hw_dma_alloc(DMA_PAGES);
                if (g_dma < 0)
                    return 0;
                if (reset_hc() != 0)
                    return 0;
                for (port = 1; port <= g_ports; port++) {
                    uint32_t speed = 0;
                    if (port_reset(port, &speed) != 0)
                        continue;
                    if (claim_port(port, speed) != 0)
                        continue;
                    if (g_kbd && g_mouse) {
                        int i;
                        for (i = 0; i < 2; i++) {
                            if (g_dev[i].live)
                                arm_intr(&g_dev[i]);
                        }
                        g_ready = 1;
                        serial_puts("xhci hid ready\n");
                        return 1;
                    }
                }
                return 0;
            }
        }
    }
    return 0;
}
