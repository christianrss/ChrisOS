#include "nvme.h"

#include "bdev.h"
#include "hwgate.h"
#include "pci.h"
#include "serial.h"

typedef struct Nvme {
    int win;
    uint32_t stride;
    int admin_sq;
    int admin_cq;
    int io_sq;
    int io_cq;
    int data;
    uint16_t asq_tail;
    uint16_t acq_head;
    uint16_t isq_tail;
    uint16_t icq_head;
    uint16_t cid;
    uint8_t admin_phase;
    uint8_t io_phase;
    uint32_t sectors;
} Nvme;

static Nvme g_nv;
static int g_ready;

static void ring_sq(Nvme *n, int admin) {
    uint32_t db = 0x1000u + (admin ? 0u : 2u * n->stride);
    hw_mmio_w32(n->win, db, admin ? n->asq_tail : n->isq_tail);
}

static void ring_cq(Nvme *n, int admin) {
    uint32_t db = 0x1000u + (admin ? n->stride : 3u * n->stride);
    hw_mmio_w32(n->win, db, admin ? n->acq_head : n->icq_head);
}

static int wait_cq(Nvme *n, int admin) {
    int q = admin ? n->admin_cq : n->io_cq;
    uint16_t head = admin ? n->acq_head : n->icq_head;
    uint8_t phase = admin ? n->admin_phase : n->io_phase;
    int spins;
    for (spins = 0; spins < 400000; ++spins) {
        uint32_t dw3 = hw_dma_r32(q, (uint32_t)head * 16u + 12u);
        if (((dw3 >> 16) & 1u) == phase) {
            uint32_t status = dw3 >> 17;
            head = (uint16_t)((head + 1u) & 1u);
            if (head == 0)
                phase = (uint8_t)(phase ^ 1u);
            if (admin) {
                n->acq_head = head;
                n->admin_phase = phase;
            } else {
                n->icq_head = head;
                n->io_phase = phase;
            }
            ring_cq(n, admin);
            return status == 0 ? 0 : -1;
        }
    }
    return -1;
}

static int admin_cmd(Nvme *n, uint32_t op, uint32_t nsid, uint32_t prp_lo,
                     uint32_t prp_hi, uint32_t cdw10, uint32_t cdw11) {
    uint32_t off = (uint32_t)n->asq_tail * 64u;
    int i;
    uint16_t cid = ++n->cid;
    for (i = 0; i < 16; ++i)
        hw_dma_w32(n->admin_sq, off + (uint32_t)i * 4u, 0);
    hw_dma_w32(n->admin_sq, off, op | ((uint32_t)cid << 16));
    hw_dma_w32(n->admin_sq, off + 4u, nsid);
    hw_dma_w32(n->admin_sq, off + 24u, prp_lo);
    hw_dma_w32(n->admin_sq, off + 28u, prp_hi);
    hw_dma_w32(n->admin_sq, off + 40u, cdw10);
    hw_dma_w32(n->admin_sq, off + 44u, cdw11);
    n->asq_tail = (uint16_t)((n->asq_tail + 1u) & 1u);
    ring_sq(n, 1);
    return wait_cq(n, 1);
}

static int io_rw(Nvme *n, int write, uint32_t lba, uint32_t count) {
    uint32_t off = (uint32_t)n->isq_tail * 64u;
    int i;
    uint16_t cid = ++n->cid;
    for (i = 0; i < 16; ++i)
        hw_dma_w32(n->io_sq, off + (uint32_t)i * 4u, 0);
    hw_dma_w32(n->io_sq, off,
               (write ? 0x01u : 0x02u) | ((uint32_t)cid << 16));
    hw_dma_w32(n->io_sq, off + 4u, 1u);
    hw_dma_w32(n->io_sq, off + 24u, hw_dma_lo(n->data));
    hw_dma_w32(n->io_sq, off + 28u, hw_dma_hi(n->data));
    hw_dma_w32(n->io_sq, off + 40u, lba);
    hw_dma_w32(n->io_sq, off + 48u, count - 1u);
    n->isq_tail = (uint16_t)((n->isq_tail + 1u) & 1u);
    ring_sq(n, 0);
    return wait_cq(n, 0);
}

static int nvme_rw(void *ctx, uint32_t lba, uint32_t count, void *buf, int write) {
    Nvme *n = (Nvme *)ctx;
    uint8_t *bytes = (uint8_t *)buf;
    while (count) {
        uint32_t nsec = count > 8u ? 8u : count;
        uint32_t i;
        if (write) {
            for (i = 0; i < nsec * 512u; i += 4) {
                uint32_t v = (uint32_t)bytes[i] | ((uint32_t)bytes[i + 1] << 8) |
                             ((uint32_t)bytes[i + 2] << 16) |
                             ((uint32_t)bytes[i + 3] << 24);
                hw_dma_w32(n->data, i, v);
            }
        }
        if (io_rw(n, write, lba, nsec) != 0)
            return BD_EIO;
        if (!write) {
            for (i = 0; i < nsec * 512u; i += 4) {
                uint32_t v = hw_dma_r32(n->data, i);
                bytes[i] = (uint8_t)v;
                bytes[i + 1] = (uint8_t)(v >> 8);
                bytes[i + 2] = (uint8_t)(v >> 16);
                bytes[i + 3] = (uint8_t)(v >> 24);
            }
        }
        bytes += nsec * 512u;
        lba += nsec;
        count -= nsec;
    }
    return BD_OK;
}

static int nvme_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    return nvme_rw(ctx, lba, count, dst, 0);
}

static int nvme_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    return nvme_rw(ctx, lba, count, (void *)src, 1);
}

int nvme_probe(void) {
    int bus;
    int dev;
    int fn;
    uint64_t cap;
    uint32_t cc;
    uint32_t nn;
    uint32_t nsze;
    uint32_t lbads;
    BlockDevice bd;
    int spins;

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
                if (((cls >> 16) & 0xFFFFu) != 0x0108u)
                    continue;
                pci_write((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4,
                          pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4) | 6u);
                g_nv.win = hw_bar_map(bus, dev, fn, 0);
                if (g_nv.win < 0)
                    continue;
                cap = (uint64_t)hw_mmio_r32(g_nv.win, 0) |
                      ((uint64_t)hw_mmio_r32(g_nv.win, 4) << 32);
                g_nv.stride = 4u << (uint32_t)((cap >> 32) & 0xFu);
                cc = hw_mmio_r32(g_nv.win, 0x14);
                hw_mmio_w32(g_nv.win, 0x14, cc & ~1u);
                for (spins = 0; spins < 200000; ++spins) {
                    if ((hw_mmio_r32(g_nv.win, 0x1C) & 1u) == 0)
                        break;
                }
                g_nv.admin_sq = hw_dma_alloc(1);
                g_nv.admin_cq = hw_dma_alloc(1);
                g_nv.io_sq = hw_dma_alloc(1);
                g_nv.io_cq = hw_dma_alloc(1);
                g_nv.data = hw_dma_alloc(1);
                if (g_nv.admin_sq < 0 || g_nv.data < 0)
                    return 0;
                g_nv.admin_phase = 1;
                g_nv.io_phase = 1;
                hw_mmio_w32(g_nv.win, 0x24, 1u | (1u << 16));
                hw_mmio_w32(g_nv.win, 0x28, hw_dma_lo(g_nv.admin_sq));
                hw_mmio_w32(g_nv.win, 0x2C, hw_dma_hi(g_nv.admin_sq));
                hw_mmio_w32(g_nv.win, 0x30, hw_dma_lo(g_nv.admin_cq));
                hw_mmio_w32(g_nv.win, 0x34, hw_dma_hi(g_nv.admin_cq));
                hw_mmio_w32(g_nv.win, 0x14, 1u | (6u << 16) | (4u << 20));
                for (spins = 0; spins < 200000; ++spins) {
                    if (hw_mmio_r32(g_nv.win, 0x1C) & 1u)
                        break;
                }
                if (admin_cmd(&g_nv, 0x06u, 0, hw_dma_lo(g_nv.data),
                              hw_dma_hi(g_nv.data), 1u, 0) != 0)
                    continue;
                nn = hw_dma_r32(g_nv.data, 516);
                if (nn < 1u)
                    continue;
                if (admin_cmd(&g_nv, 0x05u, 0, hw_dma_lo(g_nv.io_cq),
                              hw_dma_hi(g_nv.io_cq), 1u | (1u << 16), 1u) != 0)
                    continue;
                if (admin_cmd(&g_nv, 0x01u, 0, hw_dma_lo(g_nv.io_sq),
                              hw_dma_hi(g_nv.io_sq), 1u | (1u << 16),
                              1u | (1u << 16)) != 0)
                    continue;
                if (admin_cmd(&g_nv, 0x06u, 1u, hw_dma_lo(g_nv.data),
                              hw_dma_hi(g_nv.data), 0, 0) != 0)
                    continue;
                nsze = hw_dma_r32(g_nv.data, 0);
                lbads = hw_dma_r32(g_nv.data, 128);
                if (((lbads >> 16) & 0xFFu) != 9u && ((lbads >> 0) & 0xFFu) != 9u) {
                    if (nsze < 2048u)
                        continue;
                }
                if (nsze < 2048u)
                    continue;
                g_nv.sectors = nsze;
                bd.ctx = &g_nv;
                bd.sector_size = 512;
                bd.sector_count = nsze;
                bd.read = nvme_read;
                bd.write = nvme_write;
                bd.flush = 0;
                bd.writable = 1;
                bd_add("nvme", &bd);
                g_ready = 1;
                serial_puts("nvme disk sectors=");
                serial_write_u64(nsze);
                serial_puts("\n");
                return 1;
            }
        }
    }
    serial_puts("nvme miss\n");
    return 0;
}
