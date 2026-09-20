/* LEARN:STOR64-S09 */
#include "storage.h"

#include "ata_pio.h"
#include "cfs_format.h"
#include "panic.h"
#include "serial.h"
#include "storage_limits.h"

static AtaPio g_ata;
static BlockDevice g_disk;
static Cfs g_cfs;
static int g_ready;

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

int storage_init(void) {
    uint32_t reported = 0u;
    int formatted = 0;
    int rc;

    g_ready = 0;
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
        return rc;
    }
    ata_pio_make_device(&g_ata, &g_disk);
    rc = storage_format_if_empty(&g_disk, &formatted);
    if (rc == CFS_EFORMAT) {
        panic("cfs unknown disk, not formatting");
    }
    if (rc != CFS_OK) {
        panic("cfs prepare failed");
    }
    rc = cfs_mount(&g_cfs, &g_disk);
    if (rc != CFS_OK) {
        panic("cfs mount failed");
    }
    g_ready = 1;
    if (formatted) {
        serial_puts("cfs formatted then mounted\n");
    } else {
        serial_puts("cfs mounted\n");
    }
    hello_probe(&g_cfs);
    fsck_report();
    return CFS_OK;
}

int storage_ready(void) {
    return g_ready;
}

Cfs *storage_cfs(void) {
    return g_ready ? &g_cfs : 0;
}
