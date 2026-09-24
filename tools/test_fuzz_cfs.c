#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"
#include "test_disk.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_state = 0xC0FFEEu;

static uint32_t rnd(void) {
    g_state = g_state * 1664525u + 1013904223u;
    return g_state;
}

static int fake_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    memcpy(dst, (unsigned char *)ctx + (size_t)lba * STOR_SECTOR_SIZE,
           (size_t)count * STOR_SECTOR_SIZE);
    return BD_OK;
}

static int fake_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    memcpy((unsigned char *)ctx + (size_t)lba * STOR_SECTOR_SIZE, src,
           (size_t)count * STOR_SECTOR_SIZE);
    return BD_OK;
}

static int fake_flush(void *ctx) {
    (void)ctx;
    return BD_OK;
}

int main(void) {
    BlockDevice dev;
    Cfs fs;
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
    dev.writable = 1;
    if (cfs_format(&dev) != CFS_OK || cfs_mount(&fs, &dev) != CFS_OK) {
        fprintf(stderr, "fail: mount\n");
        return 1;
    }
    for (i = 0; i < 200; ++i) {
        char path[64];
        char body[16];
        uint32_t n = rnd() % 48u;
        uint32_t k;
        int rc;
        for (k = 0; k < n; ++k) {
            path[k] = (char)(32 + (rnd() % 90u));
        }
        path[n] = 0;
        body[0] = 'x';
        rc = cfs_mkdir(&fs, path);
        (void)rc;
        rc = cfs_write(&fs, path, body, 1u);
        (void)rc;
        rc = cfs_read(&fs, path, body, 16u);
        (void)rc;
        rc = cfs_unlink(&fs, path);
        (void)rc;
    }
    if (cfs_fsck(&fs) != CFS_OK) {
        fprintf(stderr, "fail: fsck after fuzz\n");
        return 1;
    }
    puts("test_fuzz_cfs: ok");
    return 0;
}
