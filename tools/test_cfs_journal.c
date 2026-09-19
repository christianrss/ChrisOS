/* LEARN:JNL64-05 */
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

static int jnl_force_commit(BlockDevice *dev, Jnl *j) {
    uint8_t s[STOR_SECTOR_SIZE];
    cfs_zero(s, STOR_SECTOR_SIZE);
    cfs_put32(s + 0, JNL_MAGIC);
    cfs_put32(s + 4, j->seq);
    cfs_put32(s + 8, JNL_COMMIT);
    cfs_put32(s + 12, j->nrec);
    cfs_put32(s + 16, cfs_checksum(s, 16u));
    return bd_write(dev, CFS_JOURNAL_LBA, 1u, s) == BD_OK ? CFS_OK : CFS_EIO;
}

static void test_geometry(void) {
    BlockDevice dev;
    uint8_t sec[STOR_SECTOR_SIZE];
    CfsSuper super;

    test_disk_reset();
    make_dev(&dev);
    expect_int("format", cfs_format(&dev), CFS_OK);
    expect_int("data lba", (int)CFS_DATA_LBA, 833);
    expect_int("bitmap bit0", (int)g_disk[(size_t)CFS_BITMAP_LBA * 512], 1);
    if (bd_read(&dev, CFS_SUPER_LBA, 1u, sec) != BD_OK) {
        fprintf(stderr, "FAIL super read\n");
        g_fails++;
        return;
    }
    expect_int("super decode", cfs_super_decode(&super, sec), 0);
    expect_int("journal lba field", (int)super.journal_lba, (int)CFS_JOURNAL_LBA);
    expect_int("journal sectors field", (int)super.journal_sectors,
               (int)CFS_JOURNAL_SECTORS);
    expect_int("journal magic",
               (int)cfs_get32(g_disk + (size_t)CFS_JOURNAL_LBA * 512),
               (int)JNL_MAGIC);
}

static void test_begin_drop(void) {
    BlockDevice dev;
    Cfs fs;
    Cfs fs2;
    Jnl j;
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsInode ghost;
    uint32_t size = 0u;
    uint16_t type = 0u;

    test_disk_reset();
    make_dev(&dev);
    expect_int("drop format", cfs_format(&dev), CFS_OK);
    expect_int("drop mount", cfs_mount(&fs, &dev), CFS_OK);

    if (bd_read(&dev, CFS_INODE_LBA, 1u, sector) != BD_OK) {
        fprintf(stderr, "FAIL inode read for drop test\n");
        g_fails++;
        return;
    }
    memset(&ghost, 0, sizeof(ghost));
    ghost.type = CFS_INODE_DIR;
    ghost.generation = 2u;
    cfs_inode_encode(sector, &ghost);

    expect_int("drop begin", jnl_begin(&j, &fs), CFS_OK);
    expect_int("drop log", jnl_log(&j, CFS_INODE_LBA, sector), CFS_OK);

    memset(&fs2, 0, sizeof(fs2));
    expect_int("drop remount", cfs_mount(&fs2, &dev), CFS_OK);
    expect_int("ghost missing", cfs_stat(&fs2, "GHOST", &size, &type), CFS_ENOENT);
    expect_int("drop journal empty",
               (int)cfs_get32(g_disk + (size_t)CFS_JOURNAL_LBA * 512 + 8),
               (int)JNL_EMPTY);
}

static void test_commit_replay(void) {
    BlockDevice dev;
    Cfs fs;
    Cfs fs2;
    Jnl j;
    uint8_t sector[STOR_SECTOR_SIZE];
    uint8_t got[STOR_SECTOR_SIZE];
    uint32_t size = 0u;

    test_disk_reset();
    make_dev(&dev);
    expect_int("replay format", cfs_format(&dev), CFS_OK);
    expect_int("replay mount", cfs_mount(&fs, &dev), CFS_OK);
    expect_int("replay write", cfs_write(&fs, "J.TXT", "replay", 6u), 6);

    if (bd_read(&dev, CFS_INODE_LBA, 1u, sector) != BD_OK) {
        fprintf(stderr, "FAIL inode read for replay setup\n");
        g_fails++;
        return;
    }
    {
        CfsInode in;
        if (cfs_inode_decode(&in, sector) != 0) {
            fprintf(stderr, "FAIL inode decode for replay setup\n");
            g_fails++;
            return;
        }
        in.generation = 99u;
        cfs_inode_encode(sector, &in);
    }
    expect_int("replay begin", jnl_begin(&j, &fs), CFS_OK);
    expect_int("replay log", jnl_log(&j, CFS_INODE_LBA, sector), CFS_OK);
    expect_int("replay commit hdr", jnl_force_commit(&dev, &j), CFS_OK);

    memset(&fs2, 0, sizeof(fs2));
    expect_int("replay remount", cfs_mount(&fs2, &dev), CFS_OK);
    if (bd_read(&dev, CFS_INODE_LBA, 1u, got) != BD_OK) {
        fprintf(stderr, "FAIL read inode after replay\n");
        g_fails++;
        return;
    }
    {
        CfsInode in;
        if (cfs_inode_decode(&in, got) != 0 || in.generation != 99u) {
            fprintf(stderr, "FAIL replay did not apply inode generation\n");
            g_fails++;
        }
    }
    expect_int("replay file ok", cfs_stat(&fs2, "J.TXT", &size, 0), CFS_OK);
    expect_int("replay size", (int)size, 6);
}

static void test_fsck_journal(void) {
    BlockDevice dev;
    Cfs fs;
    uint8_t sec[STOR_SECTOR_SIZE];

    test_disk_reset();
    make_dev(&dev);
    expect_int("fsck format", cfs_format(&dev), CFS_OK);
    expect_int("fsck mount", cfs_mount(&fs, &dev), CFS_OK);
    expect_int("fsck clean", cfs_fsck(&fs), CFS_OK);
    if (strcmp(cfs_fsck_reason(), "clean") != 0) {
        fprintf(stderr, "FAIL fsck reason %s\n", cfs_fsck_reason());
        g_fails++;
    }

    if (bd_read(&dev, CFS_JOURNAL_LBA, 1u, sec) != BD_OK) {
        fprintf(stderr, "FAIL journal read for dirty test\n");
        g_fails++;
        return;
    }
    cfs_put32(sec + 0, JNL_MAGIC);
    cfs_put32(sec + 8, JNL_BEGIN);
    cfs_put32(sec + 16, cfs_checksum(sec, 16u));
    if (bd_write(&dev, CFS_JOURNAL_LBA, 1u, sec) != BD_OK) {
        fprintf(stderr, "FAIL journal dirty write\n");
        g_fails++;
        return;
    }
    expect_int("fsck dirty", cfs_fsck(&fs) > 0 ? 1 : 0, 1);
    if (strcmp(cfs_fsck_reason(), "journal dirty") != 0) {
        fprintf(stderr, "FAIL dirty reason %s\n", cfs_fsck_reason());
        g_fails++;
    }
}

int main(void) {
    if (test_disk_init() != 0) {
        return 1;
    }
    test_geometry();
    test_begin_drop();
    test_commit_replay();
    test_fsck_journal();
    if (g_fails) {
        fprintf(stderr, "%d journal tests failed\n", g_fails);
        return 1;
    }
    printf("host journal tests passed\n");
    return 0;
}
