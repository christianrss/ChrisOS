/* LEARN:WS64-W05 */
#include "cfs.h"
#include "storage_limits.h"

#include <stdio.h>
#include <string.h>
#include "test_disk.h"

static int g_writes;

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
    g_writes += (int)count;
    return BD_OK;
}

static int fake_flush(void *ctx) {
    (void)ctx;
    return BD_OK;
}

static void make_dev(BlockDevice *d) {
    d->ctx = g_disk;
    d->sector_size = STOR_SECTOR_SIZE;
    d->sector_count = STOR_DISK_SECTORS;
    d->read = fake_read;
    d->write = fake_write;
    d->flush = fake_flush;
    d->writable = 1u;
}

int main(void) {
    BlockDevice dev;
    Cfs fs;
    int writes_before;
    uint32_t per = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    uint32_t off = (1u % per) * CFS_INODE_SIZE;

    if (test_disk_init() != 0) {
        return 1;
    }
    test_disk_reset();
    make_dev(&dev);
    if (cfs_format(&dev) != CFS_OK) {
        fprintf(stderr, "FAIL format\n");
        return 1;
    }
    if (cfs_mount(&fs, &dev) != CFS_OK) {
        fprintf(stderr, "FAIL mount\n");
        return 1;
    }
    if (cfs_write(&fs, "A.TXT", "abc", 3u) != 3) {
        fprintf(stderr, "FAIL write\n");
        return 1;
    }
    if (cfs_fsck(&fs) != 0) {
        fprintf(stderr, "FAIL clean fsck: %s\n", cfs_fsck_reason());
        return 1;
    }

    writes_before = g_writes;
    g_disk[(size_t)CFS_INODE_LBA * STOR_SECTOR_SIZE + off + 124u] ^= 0xFFu;
    if (cfs_fsck(&fs) <= 0) {
        fprintf(stderr, "FAIL corrupt inode not detected\n");
        return 1;
    }
    if (strcmp(cfs_fsck_reason(), "inode checksum") != 0) {
        fprintf(stderr, "FAIL reason %s\n", cfs_fsck_reason());
        return 1;
    }
    if (g_writes != writes_before) {
        fprintf(stderr, "FAIL fsck wrote the disk\n");
        return 1;
    }

    printf("host fsck tests passed\n");
    return 0;
}
