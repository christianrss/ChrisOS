#include "ahci.h"

#include "bdev.h"
#include "hwgate.h"
#include "pci.h"
#include "serial.h"

typedef struct AhciPort {
    int win;
    uint32_t base;
    int ctl;
    int data;
    uint32_t sectors;
} AhciPort;

static AhciPort g_port;
static int g_ready;

static int wait_bit(int win, uint32_t reg, uint32_t mask, int want) {
    int i;
    for (i = 0; i < 200000; ++i) {
        uint32_t v = hw_mmio_r32(win, reg);
        if (want) {
            if (v & mask)
                return 0;
        } else if ((v & mask) == 0) {
            return 0;
        }
    }
    return -1;
}

static int issue(AhciPort *p, int write, uint8_t cmd, uint32_t lba,
                 uint32_t count) {
    uint32_t clo;
    uint32_t chi;
    uint32_t dlo;
    uint32_t dhi;
    uint32_t ct;
    int i;
    int slot_ci;

    clo = hw_dma_lo(p->ctl);
    chi = hw_dma_hi(p->ctl);
    dlo = hw_dma_lo(p->data);
    dhi = hw_dma_hi(p->data);
    ct = clo + 0x500u;
    for (i = 0; i < 4096; i += 4)
        hw_dma_w32(p->ctl, (uint32_t)i, 0);
    hw_dma_w32(p->ctl, 0, (uint32_t)(5u | (write ? (1u << 6) : 0u) | (1u << 16)));
    hw_dma_w32(p->ctl, 8, ct);
    hw_dma_w32(p->ctl, 12, chi);
    hw_dma_w32(p->ctl, 0x500u, 0x00008027u | ((uint32_t)cmd << 16));
    hw_dma_w32(p->ctl, 0x504u, (lba & 0xFFFFFFu) | (0x40u << 24));
    hw_dma_w32(p->ctl, 0x508u, ((lba >> 24) & 0xFFFFFFu) | ((count & 0xFFu) << 24));
    hw_dma_w32(p->ctl, 0x50Cu, (count >> 8) & 0xFFu);
    hw_dma_w32(p->ctl, 0x580u, dlo);
    hw_dma_w32(p->ctl, 0x584u, dhi);
    hw_dma_w32(p->ctl, 0x58Cu, (count * 512u - 1u) | 0x80000000u);
    hw_mmio_w32(p->win, p->base + 0x10u, 0xFFFFFFFFu);
    hw_mmio_w32(p->win, p->base + 0x38u, 1u);
    slot_ci = 1;
    for (i = 0; i < 500000; ++i) {
        if ((hw_mmio_r32(p->win, p->base + 0x38u) & (uint32_t)slot_ci) == 0) {
            if (hw_mmio_r32(p->win, p->base + 0x20u) & 0x01u)
                return -1;
            return 0;
        }
    }
    return -1;
}

static int ahci_rw(void *ctx, uint32_t lba, uint32_t count, void *buf, int write) {
    AhciPort *p = (AhciPort *)ctx;
    uint8_t *bytes = (uint8_t *)buf;
    while (count) {
        uint32_t n = count > 8u ? 8u : count;
        uint32_t i;
        if (write) {
            for (i = 0; i < n * 512u; i += 4) {
                uint32_t v = (uint32_t)bytes[i] | ((uint32_t)bytes[i + 1] << 8) |
                             ((uint32_t)bytes[i + 2] << 16) |
                             ((uint32_t)bytes[i + 3] << 24);
                hw_dma_w32(p->data, i, v);
            }
        }
        if (issue(p, write, write ? 0x35 : 0x25, lba, n) != 0)
            return BD_EIO;
        if (!write) {
            for (i = 0; i < n * 512u; i += 4) {
                uint32_t v = hw_dma_r32(p->data, i);
                bytes[i] = (uint8_t)v;
                bytes[i + 1] = (uint8_t)(v >> 8);
                bytes[i + 2] = (uint8_t)(v >> 16);
                bytes[i + 3] = (uint8_t)(v >> 24);
            }
        }
        bytes += n * 512u;
        lba += n;
        count -= n;
    }
    return BD_OK;
}

static int ahci_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    return ahci_rw(ctx, lba, count, dst, 0);
}

static int ahci_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    return ahci_rw(ctx, lba, count, (void *)src, 1);
}

static int start_port(AhciPort *p) {
    uint32_t cmd;
    uint32_t clo = hw_dma_lo(p->ctl);
    uint32_t chi = hw_dma_hi(p->ctl);
    cmd = hw_mmio_r32(p->win, p->base + 0x18u);
    cmd &= ~(1u | (1u << 4));
    hw_mmio_w32(p->win, p->base + 0x18u, cmd);
    if (wait_bit(p->win, p->base + 0x18u, (1u << 15) | (1u << 14), 0) != 0)
        return -1;
    hw_mmio_w32(p->win, p->base + 0x00u, clo);
    hw_mmio_w32(p->win, p->base + 0x04u, chi);
    hw_mmio_w32(p->win, p->base + 0x08u, clo + 0x400u);
    hw_mmio_w32(p->win, p->base + 0x0Cu, chi);
    hw_mmio_w32(p->win, p->base + 0x30u, 0xFFFFFFFFu);
    cmd = hw_mmio_r32(p->win, p->base + 0x18u);
    hw_mmio_w32(p->win, p->base + 0x18u, cmd | (1u << 4) | 1u);
    return 0;
}

int ahci_probe(void) {
    int bus;
    int dev;
    int fn;
    int win;
    uint32_t pi;
    int port;
    BlockDevice bd;

    if (g_ready)
        return 1;
    for (bus = 0; bus < 8; ++bus) {
        for (dev = 0; dev < 32; ++dev) {
            for (fn = 0; fn < 8; ++fn) {
                uint32_t id = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                uint32_t cls;
                if ((id & 0xFFFFu) == 0xFFFFu)
                    continue;
                cls = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 8);
                if (((cls >> 16) & 0xFFFFu) != 0x0106u)
                    continue;
                pci_write((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4,
                          pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4) | 6u);
                win = hw_bar_map(bus, dev, fn, 5);
                if (win < 0)
                    continue;
                hw_mmio_w32(win, 4, hw_mmio_r32(win, 4) | 0x80000000u);
                pi = hw_mmio_r32(win, 0x0Cu);
                for (port = 0; port < 32; ++port) {
                    uint32_t ssts;
                    uint32_t base;
                    uint32_t words[128];
                    uint32_t sectors;
                    if ((pi & (1u << port)) == 0)
                        continue;
                    base = 0x100u + (uint32_t)port * 0x80u;
                    ssts = hw_mmio_r32(win, base + 0x28u);
                    if ((ssts & 0xFu) != 3u)
                        continue;
                    g_port.win = win;
                    g_port.base = base;
                    g_port.ctl = hw_dma_alloc(1);
                    g_port.data = hw_dma_alloc(1);
                    if (g_port.ctl < 0 || g_port.data < 0)
                        return 0;
                    if (start_port(&g_port) != 0)
                        continue;
                    if (issue(&g_port, 0, 0xEC, 0, 1) != 0)
                        continue;
                    {
                        int w;
                        for (w = 0; w < 128; ++w)
                            words[w] = hw_dma_r32(g_port.data, (uint32_t)w * 4u);
                    }
                    sectors = words[50];
                    if (sectors < 2048u)
                        sectors = words[30];
                    if (sectors < 2048u)
                        continue;
                    g_port.sectors = sectors;
                    bd.ctx = &g_port;
                    bd.sector_size = 512;
                    bd.sector_count = sectors;
                    bd.read = ahci_read;
                    bd.write = ahci_write;
                    bd.flush = 0;
                    bd.writable = 1;
                    bd_add("ahci", &bd);
                    g_ready = 1;
                    serial_puts("ahci disk sectors=");
                    serial_write_u64(sectors);
                    serial_puts("\n");
                    return 1;
                }
            }
        }
    }
    serial_puts("ahci miss\n");
    return 0;
}
