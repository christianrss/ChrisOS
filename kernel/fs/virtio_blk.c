#include "virtio_blk.h"

#include "bdev.h"
#include "hwgate.h"
#include "pci.h"
#include "serial.h"

typedef struct Vblk {
    int cwin;
    uint32_t coff;
    int nwin;
    uint32_t noff;
    int isr_win;
    uint32_t isr_off;
    int isr_ok;
    int q;
    int cmd;
    int data;
    uint16_t avail;
    uint32_t sectors;
} Vblk;

static Vblk g_blk;
static int g_ready;

static int kick(Vblk *b, int write, uint32_t lba, uint32_t bytes) {
    uint32_t clo = hw_dma_lo(b->cmd);
    uint32_t chi = hw_dma_hi(b->cmd);
    uint32_t dlo = hw_dma_lo(b->data);
    uint32_t dhi = hw_dma_hi(b->data);
    uint32_t qlo = hw_dma_lo(b->q);
    uint32_t qhi = hw_dma_hi(b->q);
    int spins;
    uint16_t idx;
    (void)qlo;
    (void)qhi;
    hw_dma_w32(b->cmd, 0, write ? 1u : 0u);
    hw_dma_w32(b->cmd, 4, 0);
    hw_dma_w32(b->cmd, 8, lba);
    hw_dma_w32(b->cmd, 12, 0);
    hw_dma_w32(b->cmd, 32, 0xFFu);
    hw_dma_w32(b->q, 0, clo);
    hw_dma_w32(b->q, 4, chi);
    hw_dma_w32(b->q, 8, 16u);
    hw_dma_w32(b->q, 12, 1u | (1u << 16));
    hw_dma_w32(b->q, 16, dlo);
    hw_dma_w32(b->q, 20, dhi);
    hw_dma_w32(b->q, 24, bytes);
    hw_dma_w32(b->q, 28, (write ? 1u : 3u) | (2u << 16));
    hw_dma_w32(b->q, 32, clo + 32u);
    hw_dma_w32(b->q, 36, chi);
    hw_dma_w32(b->q, 40, 1u);
    hw_dma_w32(b->q, 44, 2u);
    b->avail = (uint16_t)(b->avail + 1u);
    idx = b->avail;
    hw_dma_w32(b->q, 68, 0);
    hw_dma_w32(b->q, 64, (uint32_t)idx << 16);
    hw_mmio_w16(b->nwin, b->noff, 0);
    for (spins = 0; spins < 2000; ++spins) {
        uint32_t used = hw_dma_r32(b->q, 2048);
        if ((used >> 16) == idx && (hw_dma_r32(b->cmd, 32) & 0xFFu) == 0u) {
            if (b->isr_ok)
                (void)hw_mmio_r8(b->isr_win, b->isr_off);
            return 0;
        }
    }
    /* Disk writes finish on QEMU's iothread. serial_putc exits TCG long
       enough for that thread to post status 0. */
    for (spins = 0; spins < 256; ++spins) {
        uint32_t used;
        serial_putc(0);
        used = hw_dma_r32(b->q, 2048);
        if ((used >> 16) == idx && (hw_dma_r32(b->cmd, 32) & 0xFFu) == 0u) {
            if (b->isr_ok)
                (void)hw_mmio_r8(b->isr_win, b->isr_off);
            return 0;
        }
    }
    if (b->isr_ok)
        (void)hw_mmio_r8(b->isr_win, b->isr_off);
    serial_puts("vblk timeout used=");
    serial_write_u64(hw_dma_r32(b->q, 2048));
    serial_puts(" len=");
    serial_write_u64(hw_dma_r32(b->q, 2056));
    serial_puts(" st=");
    serial_write_u64(hw_dma_r32(b->cmd, 32) & 0xFFu);
    serial_puts("\n");
    return -1;
}

static int vblk_rw(void *ctx, uint32_t lba, uint32_t count, void *buf, int write) {
    Vblk *b = (Vblk *)ctx;
    uint8_t *bytes = (uint8_t *)buf;
    while (count) {
        uint32_t n = count > 8u ? 8u : count;
        uint32_t i;
        if (write) {
            for (i = 0; i < n * 512u; i += 4) {
                uint32_t v = (uint32_t)bytes[i] | ((uint32_t)bytes[i + 1] << 8) |
                             ((uint32_t)bytes[i + 2] << 16) |
                             ((uint32_t)bytes[i + 3] << 24);
                hw_dma_w32(b->data, i, v);
            }
        }
        if (kick(b, write, lba, n * 512u) != 0)
            return BD_EIO;
        if (!write) {
            for (i = 0; i < n * 512u; i += 4) {
                uint32_t v = hw_dma_r32(b->data, i);
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

static int vblk_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    return vblk_rw(ctx, lba, count, dst, 0);
}

static int vblk_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    return vblk_rw(ctx, lba, count, (void *)src, 1);
}

int virtio_blk_probe(void) {
    int bus;
    int dev;
    int fn;
    BlockDevice bd;
    if (g_ready)
        return 1;
    for (bus = 0; bus < 8; ++bus) {
        for (dev = 0; dev < 32; ++dev) {
            for (fn = 0; fn < 8; ++fn) {
                uint32_t id = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                uint32_t devid;
                int off;
                int cwin = -1;
                int nwin = -1;
                int dwin = -1;
                int iwin = -1;
                uint32_t ioff = 0;
                uint32_t coff = 0;
                uint32_t noff = 0;
                uint32_t doff = 0;
                uint32_t mult = 0;
                uint16_t qnote;
                uint32_t qlo;
                uint32_t qhi;
                uint32_t cap_lo;
                uint32_t cap_hi;
                if ((id & 0xFFFFu) != 0x1AF4u)
                    continue;
                devid = id >> 16;
                if (devid != 0x1042u && devid != 0x1001u)
                    continue;
                pci_write((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4,
                          pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4) | 6u);
                off = (int)(pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x34) &
                            0xFFu);
                while (off > 0) {
                    uint32_t cap =
                        pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, (uint8_t)off);
                    int typ = (int)((cap >> 24) & 0xFFu);
                    int next = (int)((cap >> 8) & 0xFFu);
                    if ((cap & 0xFFu) == 9u) {
                        int bar = (int)(pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                                 (uint8_t)(off + 4)) &
                                        0xFFu);
                        uint32_t cfg = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                                (uint8_t)(off + 8));
                        int win = hw_bar_map(bus, dev, fn, bar);
                        if (typ == 1) {
                            cwin = win;
                            coff = cfg;
                        }
                        if (typ == 2) {
                            nwin = win;
                            noff = cfg;
                            mult = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                            (uint8_t)(off + 16));
                        }
                        if (typ == 4) {
                            dwin = win;
                            doff = cfg;
                        }
                        if (typ == 3 && win >= 0) {
                            iwin = win;
                            ioff = cfg;
                        }
                    }
                    off = next;
                }
                if (cwin < 0 || nwin < 0 || dwin < 0)
                    continue;
                hw_mmio_w8(cwin, coff + 20, 0);
                hw_mmio_w8(cwin, coff + 20, 1);
                hw_mmio_w8(cwin, coff + 20, 3);
                hw_mmio_w32(cwin, coff, 1);
                hw_mmio_w32(cwin, coff + 8, 1);
                hw_mmio_w32(cwin, coff + 12, 1);
                hw_mmio_w8(cwin, coff + 20, 11);
                if ((hw_mmio_r8(cwin, coff + 20) & 8u) == 0)
                    continue;
                g_blk.q = hw_dma_alloc(1);
                g_blk.cmd = hw_dma_alloc(1);
                g_blk.data = hw_dma_alloc(1);
                if (g_blk.q < 0 || g_blk.cmd < 0 || g_blk.data < 0) {
                    hw_dma_free(g_blk.q);
                    hw_dma_free(g_blk.cmd);
                    hw_dma_free(g_blk.data);
                    continue;
                }
                qlo = hw_dma_lo(g_blk.q);
                qhi = hw_dma_hi(g_blk.q);
                hw_mmio_w16(cwin, coff + 22, 0);
                if (hw_mmio_r16(cwin, coff + 24) < 4u) {
                    serial_puts("virtio-blk queue too small\n");
                    continue;
                }
                hw_mmio_w16(cwin, coff + 24, 4);
                hw_mmio_w32(cwin, coff + 32, qlo);
                hw_mmio_w32(cwin, coff + 36, qhi);
                hw_mmio_w32(cwin, coff + 40, qlo + 64u);
                hw_mmio_w32(cwin, coff + 44, qhi);
                hw_mmio_w32(cwin, coff + 48, qlo + 2048u);
                hw_mmio_w32(cwin, coff + 52, qhi);
                hw_mmio_w16(cwin, coff + 28, 1);
                qnote = (uint16_t)hw_mmio_r16(cwin, coff + 30);
                g_blk.cwin = cwin;
                g_blk.coff = coff;
                g_blk.nwin = nwin;
                g_blk.noff = noff + (uint32_t)qnote * mult;
                g_blk.isr_win = iwin;
                g_blk.isr_off = ioff;
                g_blk.isr_ok = iwin >= 0;
                hw_mmio_w8(cwin, coff + 20, 15);
                cap_lo = hw_mmio_r32(dwin, doff);
                cap_hi = hw_mmio_r32(dwin, doff + 4u);
                if (cap_hi != 0u) {
                    serial_puts("virtio-blk too large\n");
                    continue;
                }
                g_blk.sectors = cap_lo;
                if (g_blk.sectors < 2048u)
                    continue;
                g_blk.avail = 0;
                bd.ctx = &g_blk;
                bd.sector_size = 512;
                bd.sector_count = g_blk.sectors;
                bd.read = vblk_read;
                bd.write = vblk_write;
                bd.flush = 0;
                bd.writable = 1;
                bd_add_kind("virtio-blk", &bd, BD_VIRTIO);
                g_ready = 1;
                serial_puts("virtio-blk sectors=");
                serial_write_u64(g_blk.sectors);
                serial_puts("\n");
                return 1;
            }
        }
    }
    serial_puts("virtio-blk miss\n");
    return 0;
}
