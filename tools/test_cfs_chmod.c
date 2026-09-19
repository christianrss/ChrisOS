/* LEARN:ACL64-02 */
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
    Cfs fs2;
    unsigned char got[8];

    if (test_disk_init() != 0) {
        return 1;
    }
    test_disk_reset();
    make_dev(&dev);
    expect_int("format", cfs_format(&dev), CFS_OK);
    expect_int("mount", cfs_mount(&fs, &dev), CFS_OK);

    expect_int("write A", cfs_write(&fs, "A.TXT", "hello", 5u), 5);
    expect_int("perm read", cfs_perm(&fs, "A.TXT", CFS_PERM_READ), CFS_OK);
    expect_int("perm write", cfs_perm(&fs, "A.TXT", CFS_PERM_WRITE), CFS_OK);
    expect_int("perm exec", cfs_perm(&fs, "A.TXT", CFS_PERM_EXEC), CFS_OK);

    expect_int("chmod read only",
               cfs_chmod(&fs, "A.TXT", CFS_PERM_READ), CFS_OK);
    expect_int("write denied",
               cfs_write(&fs, "A.TXT", "x", 1u), CFS_EPERM);
    expect_int("read ok",
               cfs_read(&fs, "A.TXT", got, 8u), 5);
    expect_int("remount", cfs_mount(&fs2, &dev), CFS_OK);
    expect_int("mode persisted",
               cfs_perm(&fs2, "A.TXT", CFS_PERM_WRITE), CFS_EPERM);
    expect_int("read after remount",
               cfs_read(&fs2, "A.TXT", got, 8u), 5);

    expect_int("restore all",
               cfs_chmod(&fs2, "A.TXT", CFS_PERM_ALL), CFS_OK);
    expect_int("write ok",
               cfs_write(&fs2, "A.TXT", "hello", 5u), 5);

    expect_int("chmod write only",
               cfs_chmod(&fs2, "A.TXT", CFS_PERM_WRITE), CFS_OK);
    expect_int("read denied",
               cfs_read(&fs2, "A.TXT", got, 8u), CFS_EPERM);

    expect_int("restore all again",
               cfs_chmod(&fs2, "A.TXT", CFS_PERM_ALL), CFS_OK);

    expect_int("mkdir GAMES", cfs_mkdir(&fs2, "GAMES"), CFS_OK);
    expect_int("no walk on GAMES",
               cfs_chmod(&fs2, "GAMES", CFS_PERM_READ | CFS_PERM_WRITE),
               CFS_OK);
    expect_int("mkdir child denied",
               cfs_mkdir(&fs2, "GAMES/CHILD"), CFS_EPERM);

    expect_int("restore walk",
               cfs_chmod(&fs2, "GAMES", CFS_PERM_ALL), CFS_OK);
    expect_int("mkdir child ok",
               cfs_mkdir(&fs2, "GAMES/CHILD"), CFS_OK);

    expect_int("mkdir BIN", cfs_mkdir(&fs2, "BIN"), CFS_OK);
    expect_int("write clv",
               cfs_write(&fs2, "BIN/DEMO.CLV", "CLV", 3u), 3);
    expect_int("exec ok",
               cfs_perm(&fs2, "BIN/DEMO.CLV", CFS_PERM_EXEC), CFS_OK);
    expect_int("no exec",
               cfs_chmod(&fs2, "BIN/DEMO.CLV",
                         CFS_PERM_READ | CFS_PERM_WRITE),
               CFS_OK);
    expect_int("exec denied",
               cfs_perm(&fs2, "BIN/DEMO.CLV", CFS_PERM_EXEC), CFS_EPERM);

    expect_int("no write for unlink",
               cfs_chmod(&fs2, "A.TXT",
                         CFS_PERM_READ | CFS_PERM_EXEC | CFS_PERM_WALK),
               CFS_OK);
    expect_int("unlink denied",
               cfs_unlink(&fs2, "A.TXT"), CFS_EPERM);
    expect_int("restore A",
               cfs_chmod(&fs2, "A.TXT", CFS_PERM_ALL), CFS_OK);
    expect_int("unlink ok",
               cfs_unlink(&fs2, "A.TXT"), CFS_OK);

    if (g_fails) {
        fprintf(stderr, "%d test(s) failed\n", g_fails);
        return 1;
    }
    puts("test_cfs_chmod: ok");
    return 0;
}
