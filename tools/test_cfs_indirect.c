/* LEARN:CFS64-V2 */
#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"

#include <stdio.h>
#include <string.h>
#include "test_disk.h"

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

static void make_dev(BlockDevice *d) {
    d->ctx = g_disk;
    d->sector_size = STOR_SECTOR_SIZE;
    d->sector_count = STOR_DISK_SECTORS;
    d->read = fake_read;
    d->write = fake_write;
    d->flush = fake_flush;
    d->writable = 1u;
}

static void expect_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
        g_fails++;
    }
}

int main(void) {
    BlockDevice dev;
    Cfs fs;
    uint32_t i;
    static unsigned char big[70000];
    static unsigned char got[70000];

    if (test_disk_init() != 0) {
        return 1;
    }
    test_disk_reset();
    make_dev(&dev);
    expect_int("format", cfs_format(&dev), CFS_OK);
    expect_int("mount", cfs_mount(&fs, &dev), CFS_OK);
    for (i = 0; i < 70000u; i++) {
        big[i] = (unsigned char)(i * 3u);
    }
    expect_int("write 70000", cfs_write(&fs, "BIG.DAT", big, 70000u), 70000);
    expect_int("read 70000", cfs_read(&fs, "BIG.DAT", got, 70000u), 70000);
    for (i = 0; i < 70000u; i++) {
        if (got[i] != big[i]) {
            fprintf(stderr, "mismatch %u\n", i);
            return 1;
        }
    }
    expect_int("fsck clean", cfs_fsck(&fs), CFS_OK);

    if (g_fails) {
        fprintf(stderr, "%d indirect tests failed\n", g_fails);
        return 1;
    }
    puts("test_cfs_indirect: ok");
    return 0;
}
