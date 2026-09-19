/* LEARN:STOR64-S02 */
#include <stdint.h>
#include "port.h"
#include "ata_pio.h"

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

static int ata_poll(const AtaPio *a, int need_drq) {
    uint32_t i;
    uint8_t s;
    for (i = 0; i < a->poll_limit; i++) {
        s = inb((uint16_t)(a->io + ATA_REG_STATUS));
        if (s == 0u || s == 0xffu)
            return BD_ENODEV;
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
        uint8_t n = (uint8_t)(count > 255u ? 255u : count);
        int rc = ata_read_chunk(a, lba, n, p);
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
        uint8_t n = (uint8_t)(count > 255u ? 255u : count);
        int rc = ata_write_chunk(a, lba, n, p);
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
}

static void ata_soft_reset(const AtaPio *a) {
    outb(a->ctrl, 0x04u);
    ata_delay_400ns(a);
    outb(a->ctrl, 0x00u);
    ata_delay_400ns(a);
}

static int ata_try_identify(AtaPio *a, uint32_t *reported_sectors) {
    uint16_t id[256];
    uint32_t i;
    int rc;

    ata_select(a, 0u);
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
    return *reported_sectors ? BD_OK : BD_EIO;
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
            ata_soft_reset(a);
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
