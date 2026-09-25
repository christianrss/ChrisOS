/* LEARN:STOR64-S09 */
#include "ahci.h"
#include "bdev.h"
#include "nvme.h"
#include "part.h"
#include "storage.h"
#include "usb_msc.h"
#include "xhci.h"
#include "virtio_blk.h"

#include "ata_pio.h"
#include "cfs_format.h"
#include "panic.h"
#include "serial.h"
#include "storage_limits.h"

static AtaPio g_ata;
static BlockDevice g_disk;
static PartView g_part;
static Cfs g_cfs;
static int g_ready;
static int g_root = -1;

static int prefix_zero(const uint8_t *p, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

int storage_format_if_empty(BlockDevice *dev, int *formatted) {
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsSuper super;
    int rc;

    if (!dev || !formatted) {
        return CFS_EINVAL;
    }
    *formatted = 0;
    rc = bd_read(dev, CFS_SUPER_LBA, 1u, sector);
    if (rc != BD_OK) {
        return CFS_EIO;
    }
    if (cfs_super_decode(&super, sector) == 0) {
        return CFS_OK;
    }
    if (prefix_zero(sector, 8u)) {
        rc = cfs_format(dev);
        if (rc != CFS_OK) {
            return rc;
        }
        *formatted = 1;
        return CFS_OK;
    }
    return CFS_EFORMAT;
}

static void hello_probe(Cfs *fs) {
    uint8_t buf[16];
    int n;
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        buf[i] = 0u;
    }
    n = cfs_read(fs, "HELLO.TXT", buf, 16u);
    if (n == CFS_ENOENT) {
        n = cfs_write(fs, "HELLO.TXT", "persistent", 10u);
        if (n == 10) {
            serial_puts("cfs hello written\n");
        } else {
            serial_puts("cfs hello write failed\n");
        }
        return;
    }
    if (n == 10 &&
        buf[0] == 'p' && buf[1] == 'e' && buf[2] == 'r' &&
        buf[3] == 's' && buf[4] == 'i' && buf[5] == 's' &&
        buf[6] == 't' && buf[7] == 'e' && buf[8] == 'n' &&
        buf[9] == 't') {
        serial_puts("cfs hello persisted\n");
        return;
    }
    serial_puts("cfs hello unexpected\n");
}

static void fsck_report(void) {
    int rc;
    const char *reason;
    if (!g_ready) {
        return;
    }
    rc = cfs_fsck(&g_cfs);
    reason = cfs_fsck_reason();
    if (rc == 0) {
        serial_puts("cfs fsck: clean\n");
        return;
    }
    serial_puts("cfs fsck: ");
    serial_puts(reason ? reason : "corrupt");
    serial_puts("\n");
}

static int disk_has_cfs(BlockDevice *dev) {
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsSuper super;
    if (!dev || bd_read(dev, CFS_SUPER_LBA, 1u, sector) != BD_OK)
        return 0;
    return cfs_super_decode(&super, sector) == 0;
}

static int claim_root(int index, BlockDevice *dev) {
    g_disk = *dev;
    g_root = index;
    bd_set_boot(index);
    bd_set_flags(index, BD_F_BOOT | BD_F_ROOT);
    serial_puts("root ");
    serial_puts(bd_name(index));
    serial_puts("\n");
    return 1;
}

static int discover_root(void) {
    int i;
    int n = bd_count();
    for (i = 0; i < n; ++i) {
        BlockDevice *d = bd_get(i);
        if (bd_kind(i) == BD_RAM)
            continue;
        if (disk_has_cfs(d))
            return claim_root(i, d);
    }
    for (i = 0; i < n; ++i) {
        BlockDevice *d = bd_get(i);
        uint32_t lba = 0;
        uint32_t count = 0;
        if (!d || bd_kind(i) == BD_RAM)
            continue;
        if (gpt_find_cfs(d, &lba, &count) != 0)
            continue;
        if (part_open(&g_part, d, lba, count) != 0)
            continue;
        if (!disk_has_cfs(&g_part.dev))
            continue;
        g_part.dev.ctx = &g_part;
        return claim_root(i, &g_part.dev);
    }
    for (i = 0; i < n; ++i) {
        BlockDevice *d = bd_get(i);
        uint8_t sector[STOR_SECTOR_SIZE];
        int formatted = 0;
        int rc;
        if (!d || !d->writable || bd_kind(i) == BD_RAM)
            continue;
        if (d->sector_count < STOR_DISK_SECTORS)
            continue;
        if (bd_read(d, 0, 1, sector) != BD_OK)
            continue;
        if (!prefix_zero(sector, 8u))
            continue;
        rc = storage_format_if_empty(d, &formatted);
        if (rc != CFS_OK)
            continue;
        return claim_root(i, d);
    }
    return 0;
}

static void bdev_rw_tests(void) {
    int i;
    int n = bd_count();
    for (i = 0; i < n; ++i) {
        BlockDevice *d = bd_get(i);
        uint8_t orig[STOR_SECTOR_SIZE];
        uint8_t got[STOR_SECTOR_SIZE];
        uint8_t pat[STOR_SECTOR_SIZE];
        uint32_t lba;
        uint32_t b;
        if (!d || !d->writable || i == g_root)
            continue;
        if (bd_kind(i) == BD_RAM || d->sector_count < 2u)
            continue;
        lba = d->sector_count - 1u;
        if (bd_read(d, lba, 1, orig) != BD_OK) {
            serial_puts("bdev rw fail ");
            serial_puts(bd_name(i));
            serial_puts("\n");
            continue;
        }
        for (b = 0; b < STOR_SECTOR_SIZE; ++b)
            pat[b] = (uint8_t)(0xA5u ^ (b & 0xFFu));
        if (bd_write(d, lba, 1, pat) != BD_OK) {
            serial_puts("bdev rw fail ");
            serial_puts(bd_name(i));
            serial_puts("\n");
            continue;
        }
        if (bd_read(d, lba, 1, got) != BD_OK) {
            serial_puts("bdev rw fail ");
            serial_puts(bd_name(i));
            serial_puts("\n");
            (void)bd_write(d, lba, 1, orig);
            continue;
        }
        for (b = 0; b < STOR_SECTOR_SIZE; ++b) {
            if (got[b] != pat[b])
                break;
        }
        (void)bd_write(d, lba, 1, orig);
        serial_puts(b == STOR_SECTOR_SIZE ? "bdev rw ok " : "bdev rw fail ");
        serial_puts(bd_name(i));
        serial_puts("\n");
    }
}

int storage_init(void) {
    uint32_t reported = 0u;
    int formatted = 0;
    int rc;
    BlockDevice ata;

    g_ready = 0;
    g_root = -1;
    ata_pio_configure(&g_ata, STOR_DISK_SECTORS);
    rc = ata_pio_identify(&g_ata, &reported);
    if (rc != BD_OK) {
        serial_puts("ata missing rc=");
        if (rc < 0) {
            serial_puts("-");
            serial_write_u64((uint64_t)(-rc));
        } else {
            serial_write_u64((uint64_t)rc);
        }
        serial_puts("\n");
    } else {
        if (reported >= 2048u)
            g_ata.sectors = reported;
        ata_pio_make_device(&g_ata, &ata);
        bd_add_kind("ata", &ata, BD_ATA);
    }
    (void)ahci_probe();
    (void)nvme_probe();
    (void)virtio_blk_probe();
    (void)usb_msc_probe();
    (void)xhci_hid_probe();
    serial_puts("disk scan\n");
    if (!discover_root()) {
        serial_puts("root miss disks=");
        serial_write_u64((uint64_t)bd_count());
        serial_puts("\n");
        panic("no root disk");
    }
    rc = storage_format_if_empty(&g_disk, &formatted);
    if (rc == CFS_EFORMAT) {
        panic("cfs unknown disk, not formatting");
    }
    if (rc != CFS_OK) {
        panic("cfs prepare failed");
    }
    rc = cfs_mount(&g_cfs, &g_disk);
    if (rc != CFS_OK) {
        serial_puts("cfs mount rc=");
        serial_write_u64((uint64_t)(rc < 0 ? -rc : rc));
        serial_puts(" sectors=");
        serial_write_u64(g_disk.sector_count);
        serial_puts("\n");
        panic("cfs mount failed");
    }
    g_ready = 1;
    if (formatted) {
        serial_puts("cfs formatted then mounted\n");
    } else {
        serial_puts("cfs mounted\n");
    }
    hello_probe(&g_cfs);
    if (g_cfs.super.clean) {
        serial_puts("cfs fsck: skipped clean\n");
    } else {
        fsck_report();
    }
    bdev_rw_tests();
    return CFS_OK;
}

int storage_ready(void) {
    return g_ready;
}

Cfs *storage_cfs(void) {
    return g_ready ? &g_cfs : 0;
}

BlockDevice *storage_disk(void) {
    return g_ready ? &g_disk : 0;
}

int storage_reformat(void) {
    int rc;

    if (!g_ready) {
        return -1;
    }
    rc = cfs_format(&g_disk);
    if (rc != CFS_OK) {
        return rc;
    }
    rc = cfs_mount(&g_cfs, &g_disk);
    return rc == CFS_OK ? 0 : rc;
}
