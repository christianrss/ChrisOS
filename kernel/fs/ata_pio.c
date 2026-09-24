/* LEARN:STOR64-S02 */
#include <stdint.h>
#include "port.h"
#include "ata_pio.h"
#include "pci.h"
#include "irq.h"
#include "pmm.h"
#include "bootinfo.h"
#include "serial.h"

#define ATA_REG_DATA       0u
#define ATA_REG_COUNT      2u
#define ATA_REG_LBA0       3u
#define ATA_REG_LBA1       4u
#define ATA_REG_LBA2       5u
#define ATA_REG_DRIVE      6u
#define ATA_REG_STATUS     7u
#define ATA_REG_COMMAND    7u

#define ATA_SR_ERR         0x01u
#define ATA_SR_DRQ         0x08u
#define ATA_SR_DF          0x20u
#define ATA_SR_BSY         0x80u

#define ATA_CMD_READ       0x20u
#define ATA_CMD_WRITE      0x30u
#define ATA_CMD_FLUSH      0xe7u
#define ATA_CMD_IDENTIFY   0xecu
#define ATA_CMD_READ_DMA   0xc8u
#define ATA_CMD_WRITE_DMA  0xcau

static volatile int g_ide_irq;
static uint16_t g_bm_io;

static void ide_irq(struct irq_frame *frame) {
    uint8_t st;
    (void)frame;
    g_ide_irq = 1;
    if (g_bm_io != 0) {
        st = inb((uint16_t)(g_bm_io + 2u));
        outb((uint16_t)(g_bm_io + 2u), st);
    }
}

static void ata_program(const AtaPio *a, uint32_t lba,
                        uint8_t count, uint8_t command);
static void ata_soft_reset_port(uint16_t ctrl);
static int ata_wait_not_busy(const AtaPio *a);

static int ata_dma_wait(uint16_t bm) {
    uint32_t i;
    int saw_idle = 0;
    for (i = 0; i < 200000u; ++i) {
        uint8_t st = inb((uint16_t)(bm + 2u));
        /* A stuck interrupt bit must not count as completion. The bit has
           to fall, then rise, after the command is started. */
        if ((st & 0x04u) == 0 && !g_ide_irq)
            saw_idle = 1;
        if (saw_idle && (g_ide_irq || (st & 0x04u))) {
            if (st & 0x02u)
                return BD_EIO;
            return BD_OK;
        }
        if ((i & 63u) == 0)
            (void)inb(0x80);
    }
    return BD_ETIMEOUT;
}

static void ata_dma_abort(AtaPio *a) {
    if (a->bm != 0)
        outb(a->bm, 0);
    ata_soft_reset_port(a->ctrl);
    (void)ata_wait_not_busy(a);
}

static int ata_dma_xfer(AtaPio *a, uint32_t lba, uint8_t count,
                        uint8_t *buf, int write) {
    uint32_t bytes;
    uint64_t pages;
    uint64_t phys;
    uint64_t prdt_phys;
    uint8_t *dst;
    uint32_t *prdt;
    uint8_t cmd;
    int rc;

    if (!a->dma || a->bm == 0 || count == 0 || count > 16u) {
        return BD_ENODEV;
    }
    bytes = (uint32_t)count * 512u;
    pages = (bytes + PMM_PAGE - 1ull) / PMM_PAGE;
    phys = pmm_alloc_contig(pages);
    prdt_phys = pmm_alloc();
    if (phys == 0 || prdt_phys == 0 || phys > 0xffffffffull ||
        prdt_phys > 0xffffffffull) {
        if (phys)
            pmm_free_contig(phys, pages);
        if (prdt_phys)
            pmm_free(prdt_phys);
        return BD_EIO;
    }
    dst = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
    if (write) {
        uint32_t i;
        for (i = 0; i < bytes; ++i) {
            dst[i] = buf[i];
        }
    }
    prdt = (uint32_t *)(uintptr_t)bootinfo_phys_to_virt(prdt_phys);
    prdt[0] = (uint32_t)phys;
    prdt[1] = bytes | 0x80000000u;
    outb((uint16_t)(a->bm + 0u), 0);
    outl((uint16_t)(a->bm + 4u), (uint32_t)prdt_phys);
    outb((uint16_t)(a->bm + 2u), 0x06u);
    cmd = write ? 0x00u : 0x08u;
    outb((uint16_t)(a->bm + 0u), cmd);
    g_ide_irq = 0;
    ata_program(a, lba, count, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
    outb((uint16_t)(a->bm + 0u), (uint8_t)(cmd | 0x01u));
    rc = ata_dma_wait(a->bm);
    outb((uint16_t)(a->bm + 0u), 0);
    if (rc == BD_OK && !write) {
        uint32_t i;
        for (i = 0; i < bytes; ++i) {
            buf[i] = dst[i];
        }
    }
    pmm_free_contig(phys, pages);
    pmm_free(prdt_phys);
    return rc;
}

static uint16_t ata_inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void ata_outw(uint16_t port, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

static void ata_delay_400ns(const AtaPio *a) {
    (void)inb(a->ctrl);
    (void)inb(a->ctrl);
    (void)inb(a->ctrl);
    (void)inb(a->ctrl);
}

static int ata_wait_not_busy(const AtaPio *a) {
    uint32_t i;
    uint8_t s;
    for (i = 0; i < a->poll_limit; i++) {
        s = inb(a->ctrl);
        if (s == 0xffu)
            return BD_ENODEV;
        if (!(s & ATA_SR_BSY))
            return BD_OK;
        __asm__ volatile ("pause");
    }
    return BD_ETIMEOUT;
}

static int ata_poll(const AtaPio *a, int need_drq) {
    uint32_t i;
    uint8_t s;
    uint32_t idle = 0;
    int saw_bsy = 0;
    for (i = 0; i < a->poll_limit; i++) {
        s = inb((uint16_t)(a->io + ATA_REG_STATUS));
        if (s == 0xffu)
            return BD_ENODEV;
        if (s & ATA_SR_BSY)
            saw_bsy = 1;
        /* An empty bus stays 0 and never raises BSY. Burning poll_limit
         * here kept the no-disk boot inside identify until the gate timed out. */
        if (s == 0 && !saw_bsy) {
            if (++idle > 256u)
                return BD_ENODEV;
        } else {
            idle = 0;
        }
        if (s & (ATA_SR_ERR | ATA_SR_DF))
            return BD_EIO;
        if (!(s & ATA_SR_BSY)) {
            if (!need_drq || (s & ATA_SR_DRQ))
                return BD_OK;
        }
        __asm__ volatile ("pause");
    }
    return BD_ETIMEOUT;
}

static void ata_select(const AtaPio *a, uint32_t lba) {
    outb((uint16_t)(a->io + ATA_REG_DRIVE),
         (uint8_t)(0xe0u | (a->drive << 4) | ((lba >> 24) & 0x0fu)));
    ata_delay_400ns(a);
}

static void ata_program(const AtaPio *a, uint32_t lba,
                        uint8_t count, uint8_t command) {
    ata_select(a, lba);
    outb((uint16_t)(a->io + ATA_REG_COUNT), count);
    outb((uint16_t)(a->io + ATA_REG_LBA0), (uint8_t)lba);
    outb((uint16_t)(a->io + ATA_REG_LBA1), (uint8_t)(lba >> 8));
    outb((uint16_t)(a->io + ATA_REG_LBA2), (uint8_t)(lba >> 16));
    outb((uint16_t)(a->io + ATA_REG_COMMAND), command);
}

static int ata_read_chunk(AtaPio *a, uint32_t lba,
                          uint8_t count, uint8_t *dst) {
    uint32_t s, w;
    int rc;
    ata_program(a, lba, count, ATA_CMD_READ);
    for (s = 0; s < (uint32_t)count; s++) {
        rc = ata_poll(a, 1);
        if (rc != BD_OK)
            return rc;
        for (w = 0; w < 256u; w++) {
            uint16_t v = ata_inw((uint16_t)(a->io + ATA_REG_DATA));
            dst[s * 512u + w * 2u] = (uint8_t)v;
            dst[s * 512u + w * 2u + 1u] = (uint8_t)(v >> 8);
        }
    }
    return BD_OK;
}

static int ata_write_chunk(AtaPio *a, uint32_t lba,
                           uint8_t count, const uint8_t *src) {
    uint32_t s, w;
    int rc;
    ata_program(a, lba, count, ATA_CMD_WRITE);
    for (s = 0; s < (uint32_t)count; s++) {
        rc = ata_poll(a, 1);
        if (rc != BD_OK)
            return rc;
        for (w = 0; w < 256u; w++) {
            uint32_t p = s * 512u + w * 2u;
            uint16_t v = (uint16_t)src[p] |
                         (uint16_t)((uint16_t)src[p + 1u] << 8);
            ata_outw((uint16_t)(a->io + ATA_REG_DATA), v);
        }
    }
    return BD_OK;
}

static int ata_flush_raw(AtaPio *a) {
    ata_select(a, 0u);
    outb((uint16_t)(a->io + ATA_REG_COMMAND), ATA_CMD_FLUSH);
    return ata_poll(a, 0);
}

static int ata_bd_read(void *ctx, uint32_t lba,
                       uint32_t count, void *dst) {
    AtaPio *a = ctx;
    uint8_t *p = dst;
    while (count) {
        uint8_t n = (uint8_t)(count > 16u ? 16u : count);
        int rc = ata_dma_xfer(a, lba, n, p, 0);
        if (rc != BD_OK) {
            ata_dma_abort(a);
            n = (uint8_t)(count > 255u ? 255u : count);
            rc = ata_read_chunk(a, lba, n, p);
        }
        if (rc != BD_OK)
            return rc;
        lba += n;
        count -= n;
        p += (uint32_t)n * 512u;
    }
    return BD_OK;
}

static int ata_bd_write(void *ctx, uint32_t lba,
                        uint32_t count, const void *src) {
    AtaPio *a = ctx;
    const uint8_t *p = src;
    while (count) {
        uint8_t n = (uint8_t)(count > 16u ? 16u : count);
        int rc = ata_dma_xfer(a, lba, n, (uint8_t *)p, 1);
        if (rc != BD_OK) {
            ata_dma_abort(a);
            n = (uint8_t)(count > 255u ? 255u : count);
            rc = ata_write_chunk(a, lba, n, p);
        }
        if (rc != BD_OK)
            return rc;
        lba += n;
        count -= n;
        p += (uint32_t)n * 512u;
    }
    return ata_flush_raw(a);
}

static int ata_bd_flush(void *ctx) {
    return ata_flush_raw((AtaPio *)ctx);
}

void ata_pio_configure(AtaPio *a, uint32_t sectors) {
    a->io = 0x1f0u;
    a->ctrl = 0x3f6u;
    a->drive = 0u;
    a->sectors = sectors;
    a->poll_limit = 1000000u;
    a->bm = 0;
    a->dma = 0;
}

static void ata_soft_reset_port(uint16_t ctrl) {
    outb(ctrl, 0x04u);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    outb(ctrl, 0x02u);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
}

static void ata_soft_reset_all(void) {
    ata_soft_reset_port(0x3f6u);
    ata_soft_reset_port(0x376u);
}

static int ata_try_identify(AtaPio *a, uint32_t *reported_sectors) {
    uint16_t id[256];
    uint32_t i;
    int rc;

    ata_select(a, 0u);
    rc = ata_wait_not_busy(a);
    if (rc != BD_OK)
        return rc;
    outb((uint16_t)(a->io + ATA_REG_COUNT), 0u);
    outb((uint16_t)(a->io + ATA_REG_LBA0), 0u);
    outb((uint16_t)(a->io + ATA_REG_LBA1), 0u);
    outb((uint16_t)(a->io + ATA_REG_LBA2), 0u);
    outb((uint16_t)(a->io + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    rc = ata_poll(a, 1);
    if (rc != BD_OK) {
        return rc;
    }
    for (i = 0; i < 256u; i++) {
        id[i] = ata_inw((uint16_t)(a->io + ATA_REG_DATA));
    }
    if (!(id[49] & (1u << 9))) {
        return BD_EIO;
    }
    *reported_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    /* Word 83 bit 10: LBA48. Words 100-101 are the low 32 bits of the
       real sector count; words 60-61 saturate near 128 GiB. */
    if ((id[83] & (1u << 10)) != 0) {
        uint32_t hi = (uint32_t)id[102] | ((uint32_t)id[103] << 16);
        uint32_t lo = (uint32_t)id[100] | ((uint32_t)id[101] << 16);
        if (hi != 0u) {
            serial_puts("ata disk too large\n");
            return BD_EIO;
        }
        if (lo >= 2048u)
            *reported_sectors = lo;
    }
    if ((id[49] & (1u << 8)) != 0) {
        uint16_t bm = 0;
        if (pci_find_ide(&bm)) {
            a->bm = bm;
            a->dma = 1;
            g_bm_io = bm;
            irq_set_handler(14, ide_irq);
            pic_set_mask(14, false);
            serial_puts("ata dma bm=");
            serial_write_hex(bm);
            serial_puts("\n");
        }
    }
    return BD_OK;
}

int ata_pio_identify(AtaPio *a, uint32_t *reported_sectors) {
    static const struct {
        uint16_t io;
        uint16_t ctrl;
    } ports[] = {
        {0x1f0u, 0x3f6u},
        {0x170u, 0x376u},
    };
    uint32_t port;
    uint32_t drive;
    uint32_t i;
    uint32_t attempt;
    int rc = BD_ENODEV;

    for (i = 0; i < 5000000u; i++) {
        __asm__ volatile ("pause");
    }
    for (attempt = 0; attempt < 8u; attempt++) {
        if (attempt > 0u) {
            ata_soft_reset_all();
            a->io = ports[0].io;
            a->ctrl = ports[0].ctrl;
            a->drive = 0u;
            (void)ata_wait_not_busy(a);
            for (i = 0; i < 200000u; i++) {
                __asm__ volatile ("pause");
            }
        }
        for (port = 0u; port < 2u; port++) {
            a->io = ports[port].io;
            a->ctrl = ports[port].ctrl;
            for (drive = 0u; drive < 2u; drive++) {
                a->drive = drive;
                rc = ata_try_identify(a, reported_sectors);
                if (rc == BD_OK) {
                    return BD_OK;
                }
            }
        }
    }
    return rc;
}

void ata_pio_make_device(AtaPio *a, BlockDevice *out) {
    out->ctx = a;
    out->sector_size = 512u;
    out->sector_count = a->sectors;
    out->read = ata_bd_read;
    out->write = ata_bd_write;
    out->flush = ata_bd_flush;
    out->writable = 1u;
}
