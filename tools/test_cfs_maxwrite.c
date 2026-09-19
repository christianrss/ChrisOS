/* Boundary probe: last ok size and first fail for CFS_MAX_FILE_SIZE */
#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"
#include "test_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;

static int fake_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    unsigned char *disk = ctx;
    memcpy(dst, disk + (size_t)lba * STOR_SECTOR_SIZE,
           (size_t)count * STOR_SECTOR_SIZE);
    return BD_OK;
}

static int fake_write(void *ctx, uint32_t lba, uint32_t count,
                      const void *src) {
    unsigned char *disk = ctx;
    memcpy(disk + (size_t)lba * STOR_SECTOR_SIZE, src,
           (size_t)count * STOR_SECTOR_SIZE);
    return BD_OK;
}

static int fake_flush(void *ctx) {
    (void)ctx;
    return BD_OK;
}

static void expect_ok(const char *label, int rc, int want) {
    if (rc != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", label, rc, want);
        g_fails++;
    }
}

int main(void) {
    BlockDevice dev;
    Cfs fs;
    unsigned char *buf;
    uint32_t sizes[] = {
        CFS_MAX_FILE_SIZE,
        CFS_MAX_FILE_SIZE + 1u
    };
    int i;

    if (test_disk_init() != 0) {
        return 1;
    }
    test_disk_reset();
    memset(&dev, 0, sizeof(dev));
    dev.ctx = g_disk;
    dev.sector_size = STOR_SECTOR_SIZE;
    dev.sector_count = STOR_DISK_SECTORS;
    dev.read = fake_read;
    dev.write = fake_write;
    dev.flush = fake_flush;
    dev.writable = 1u;

    expect_ok("format", cfs_format(&dev), CFS_OK);
    expect_ok("mount", cfs_mount(&fs, &dev), CFS_OK);

    buf = (unsigned char *)malloc((size_t)CFS_MAX_FILE_SIZE + 1u);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    memset(buf, 0xAB, (size_t)CFS_MAX_FILE_SIZE + 1u);

    for (i = 0; i < 2; i++) {
        char name[16];
        int rc;
        uint32_t sz = sizes[i];

        snprintf(name, sizeof(name), "T%u.DAT", (unsigned)i);
        rc = cfs_write(&fs, name, buf, sz);
        printf("size %u rc %d (max=%u)\n", (unsigned)sz, rc, (unsigned)CFS_MAX_FILE_SIZE);
        if (sz <= CFS_MAX_FILE_SIZE) {
            expect_ok("write ok", rc, (int)sz);
        } else {
            expect_ok("write efbig", rc, CFS_EFBIG);
        }
    }

    free(buf);
    if (g_fails) {
        return 1;
    }
    puts("test_cfs_maxwrite: ok");
    return 0;
}
