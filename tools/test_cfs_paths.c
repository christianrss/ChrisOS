/* LEARN:WS64-W05 */
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

static int count_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    int *n = ctx;
    (void)name;
    (void)size;
    (void)type;
    (*n)++;
    return 0;
}

static int find_games(void *ctx, const char *name, uint32_t size,
                      uint16_t type) {
    int *found = ctx;
    (void)size;
    if (name[0] == 'G' && name[1] == 'A' && name[2] == 'M' &&
        name[3] == 'E' && name[4] == 'S' && name[5] == 0 &&
        type == CFS_INODE_DIR) {
        *found = 1;
    }
    return 0;
}

int main(void) {
    BlockDevice dev;
    Cfs fs;
    Cfs fs2;
    unsigned char got[16];
    uint32_t size;
    uint16_t type;
    int n;
    int found;

    if (test_disk_init() != 0) {
        return 1;
    }
    test_disk_reset();
    make_dev(&dev);
    expect_int("format", cfs_format(&dev), CFS_OK);
    expect_int("mount", cfs_mount(&fs, &dev), CFS_OK);
    expect_int("mkdir GAMES", cfs_mkdir(&fs, "GAMES"), CFS_OK);
    expect_int("mkdir SRC", cfs_mkdir(&fs, "SRC"), CFS_OK);
    expect_int("mkdir BIN", cfs_mkdir(&fs, "BIN"), CFS_OK);
    expect_int("mkdir again", cfs_mkdir(&fs, "GAMES"), CFS_EEXIST);
    expect_int("write path",
               cfs_write(&fs, "GAMES/A.TXT", "hello", 5u), 5);
    expect_int("write slash",
               cfs_write(&fs, "/SRC/B.TXT", "world", 5u), 5);
    expect_int("case a", cfs_write(&fs, "GAMES/foo", "lo", 2u), 2);
    expect_int("case A", cfs_write(&fs, "GAMES/Foo", "HI", 2u), 2);
    memset(got, 0, sizeof(got));
    expect_int("read foo", cfs_read(&fs, "GAMES/foo", got, 16u), 2);
    expect_int("foo byte", (int)got[0], (int)'l');
    memset(got, 0, sizeof(got));
    expect_int("read Foo", cfs_read(&fs, "GAMES/Foo", got, 16u), 2);
    expect_int("Foo byte", (int)got[0], (int)'H');
    expect_int("unlink foo", cfs_unlink(&fs, "GAMES/foo"), CFS_OK);
    expect_int("unlink Foo", cfs_unlink(&fs, "GAMES/Foo"), CFS_OK);
    memset(got, 0, sizeof(got));
    expect_int("read path", cfs_read(&fs, "GAMES/A.TXT", got, 16u), 5);
    expect_int("byte0", (int)got[0], (int)'h');
    expect_int("stat dir", cfs_stat(&fs, "GAMES", &size, &type), CFS_OK);
    expect_int("type dir", (int)type, (int)CFS_INODE_DIR);
    expect_int("stat file", cfs_stat(&fs, "GAMES/A.TXT", &size, &type), CFS_OK);
    expect_int("type file", (int)type, (int)CFS_INODE_FILE);
    expect_int("size file", (int)size, 5);
    found = 0;
    expect_int("list root", cfs_list(&fs, find_games, &found) >= 3, 1);
    expect_int("found GAMES", found, 1);
    n = 0;
    expect_int("list GAMES", cfs_list_at(&fs, "GAMES", count_cb, &n), 1);
    expect_int("rename same dir",
               cfs_rename(&fs, "GAMES/A.TXT", "GAMES/C.TXT"), CFS_OK);
    expect_int("old gone", cfs_read(&fs, "GAMES/A.TXT", got, 16u), CFS_ENOENT);
    expect_int("new there", cfs_read(&fs, "GAMES/C.TXT", got, 16u), 5);
    expect_int("rename cross",
               cfs_rename(&fs, "GAMES/C.TXT", "SRC/C.TXT"), CFS_OK);
    expect_int("read cross", cfs_read(&fs, "SRC/C.TXT", got, 16u), 5);
    expect_int("gone from games",
               cfs_read(&fs, "GAMES/C.TXT", got, 16u), CFS_ENOENT);
    expect_int("unlink file", cfs_unlink(&fs, "SRC/C.TXT"), CFS_OK);
    expect_int("rmdir nonempty", cfs_rmdir(&fs, "SRC"), CFS_ENOTEMPTY);
    expect_int("unlink B", cfs_unlink(&fs, "SRC/B.TXT"), CFS_OK);
    expect_int("rmdir SRC", cfs_rmdir(&fs, "SRC"), CFS_OK);
    expect_int("dot rejected", cfs_mkdir(&fs, "GAMES/."), CFS_EINVAL);
    expect_int("dotdot rejected", cfs_mkdir(&fs, ".."), CFS_EINVAL);
    {
        char deep[CFS_PATH_MAX];
        int pos = 0;
        int c;
        for (c = 0; c < 33; c++) {
            if (c > 0) {
                deep[pos++] = '/';
            }
            deep[pos++] = 'A';
        }
        deep[pos] = 0;
        expect_int("depth", cfs_mkdir(&fs, deep), CFS_EINVAL);
    }
    expect_int("fsck clean", cfs_fsck(&fs), CFS_OK);

    memset(&fs2, 0, sizeof(fs2));
    expect_int("remount", cfs_mount(&fs2, &dev), CFS_OK);
    memset(got, 0, sizeof(got));
    expect_int("persist missing",
               cfs_read(&fs2, "GAMES/A.TXT", got, 16u), CFS_ENOENT);
    n = 0;
    expect_int("persist GAMES", cfs_list_at(&fs2, "GAMES", count_cb, &n), 0);
    expect_int("persist fsck", cfs_fsck(&fs2), CFS_OK);

    if (g_fails) {
        fprintf(stderr, "%d path tests failed\n", g_fails);
        return 1;
    }
    printf("host cfs path tests passed\n");
    return 0;
}
