#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_disk.h"

static uint32_t g_fail_lba = 0xFFFFFFFFu;
static int g_fails;

static int fake_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    unsigned char *disk = ctx;
    if (g_fail_lba >= lba && g_fail_lba < lba + count) {
        return BD_EIO;
    }
    memcpy(dst, disk + (size_t)lba * STOR_SECTOR_SIZE,
           (size_t)count * STOR_SECTOR_SIZE);
    return BD_OK;
}

static int fake_write(void *ctx, uint32_t lba, uint32_t count,
                      const void *src) {
    unsigned char *disk = ctx;
    if (g_fail_lba >= lba && g_fail_lba < lba + count) {
        return BD_EIO;
    }
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

static int prefix_zero(const uint8_t *p, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int format_if_empty(BlockDevice *dev, int *formatted) {
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsSuper super;
    int rc;
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

static void reset_disk(void) {
    test_disk_reset();
    g_fail_lba = 0xFFFFFFFFu;
}

static void test_reboot_persist(void) {
    BlockDevice dev;
    Cfs fs;
    unsigned char got[16];
    int formatted = 0;

    reset_disk();
    make_dev(&dev);
    expect_int("empty prepare", format_if_empty(&dev, &formatted), CFS_OK);
    expect_int("did format", formatted, 1);
    expect_int("mount1", cfs_mount(&fs, &dev), CFS_OK);
    expect_int("write hello",
               cfs_write(&fs, "HELLO.TXT", "persistent", 10u), 10);
    memset(&fs, 0, sizeof(fs));
    formatted = 0;
    expect_int("second prepare", format_if_empty(&dev, &formatted), CFS_OK);
    expect_int("no reformat", formatted, 0);
    expect_int("mount2", cfs_mount(&fs, &dev), CFS_OK);
    memset(got, 0, sizeof(got));
    expect_int("read hello", cfs_read(&fs, "HELLO.TXT", got, 16u), 10);
    expect_int("hello0", (int)got[0], (int)'p');
    expect_int("hello9", (int)got[9], (int)'t');
    memset(got, 0, sizeof(got));
    expect_int("read hello at", cfs_read_at(&fs, "HELLO.TXT", 3u, got, 4u), 4);
    expect_int("hello at 0", (int)got[0], (int)'s');
    expect_int("hello at 3", (int)got[3], (int)'t');
    if (cfs_cache_hits(&fs) == 0u || cfs_cache_misses(&fs) == 0u) {
        fprintf(stderr, "FAIL cache counters hits=%llu misses=%llu\n",
                (unsigned long long)cfs_cache_hits(&fs),
                (unsigned long long)cfs_cache_misses(&fs));
        g_fails++;
    }
}

static void make_name(char *out, int n) {
    out[0] = 'F';
    out[1] = (char)('0' + (n / 100) % 10);
    out[2] = (char)('0' + (n / 10) % 10);
    out[3] = (char)('0' + n % 10);
    out[4] = 0;
}

static void test_disk_full(void) {
    BlockDevice dev;
    Cfs fs;
    char path[16];
    char name[8];
    unsigned char one = 'x';
    int d;
    int i;
    int rc;

    reset_disk();
    make_dev(&dev);
    expect_int("full format", cfs_format(&dev), CFS_OK);
    expect_int("full mount", cfs_mount(&fs, &dev), CFS_OK);
    for (d = 0; d < 3; d++) {
        snprintf(path, sizeof(path), "B%d", d);
        expect_int("mkdir batch", cfs_mkdir(&fs, path), CFS_OK);
        for (i = 0; i < 65; i++) {
            snprintf(path, sizeof(path), "B%d/F%03d", d, i);
            rc = cfs_write(&fs, path, &one, 1u);
            if (rc != 1) {
                fprintf(stderr, "FAIL batch %d/%d rc %d\n", d, i, rc);
                g_fails++;
                return;
            }
        }
    }
    for (i = 0; i < 69; i++) {
        make_name(name, i);
        rc = cfs_write(&fs, name, &one, 1u);
        if (rc != 1) {
            fprintf(stderr, "FAIL root %d rc %d\n", i, rc);
            g_fails++;
            return;
        }
    }
    make_name(name, 69);
    expect_int("70th in root ok", cfs_write(&fs, name, &one, 1u), 1);
    for (i = 70; i < 140; i++) {
        make_name(name, i);
        rc = cfs_write(&fs, name, &one, 1u);
        if (rc != 1) {
            fprintf(stderr, "FAIL root extra %d rc %d\n", i, rc);
            g_fails++;
            return;
        }
    }
    make_name(name, 0);
    expect_int("first still there", cfs_read(&fs, name, &one, 1u), 1);
}

static void test_io_error(void) {
    BlockDevice dev;
    Cfs fs;

    reset_disk();
    make_dev(&dev);
    expect_int("io format", cfs_format(&dev), CFS_OK);
    g_fail_lba = CFS_SUPER_LBA;
    expect_int("mount EIO", cfs_mount(&fs, &dev), CFS_EIO);
    g_fail_lba = 0xFFFFFFFFu;
    expect_int("mount after clear", cfs_mount(&fs, &dev), CFS_OK);
    g_fail_lba = CFS_BITMAP_LBA;
    expect_int("write bitmap EIO",
               cfs_write(&fs, "Z.TXT", "a", 1u), CFS_EIO);
}

static void test_unknown_magic(void) {
    BlockDevice dev;
    Cfs fs;
    int formatted = 0;
    uint32_t i;
    uint8_t before[8];

    reset_disk();
    make_dev(&dev);
    for (i = 0; i < 8u; i++) {
        g_disk[i] = 0xFFu;
        before[i] = 0xFFu;
    }
    expect_int("unknown prepare", format_if_empty(&dev, &formatted),
               CFS_EFORMAT);
    expect_int("unknown did not format", formatted, 0);
    for (i = 0; i < 8u; i++) {
        if (g_disk[i] != before[i]) {
            fprintf(stderr, "FAIL unknown magic mutated byte %u\n", i);
            g_fails++;
        }
    }
    expect_int("unknown mount", cfs_mount(&fs, &dev), CFS_EFORMAT);
    expect_int("still FF", (int)g_disk[0], 0xFF);
}

static void test_max_file(void) {
    BlockDevice dev;
    Cfs fs;
    unsigned char *big;
    uint32_t i;
    uint32_t probe = 2u * 1024u * 1024u;

    reset_disk();
    make_dev(&dev);
    expect_int("max format", cfs_format(&dev), CFS_OK);
    expect_int("max mount", cfs_mount(&fs, &dev), CFS_OK);
    big = (unsigned char *)malloc((size_t)CFS_MAX_FILE_SIZE + 2u);
    if (!big) {
        fprintf(stderr, "FAIL max_file: malloc %u\n",
                (unsigned)(CFS_MAX_FILE_SIZE + 2u));
        g_fails++;
        return;
    }
    for (i = 0; i < CFS_MAX_FILE_SIZE; i++) {
        big[i] = (unsigned char)(i & 0xFFu);
    }
    expect_int("write 2MiB",
               cfs_write(&fs, "BIG.TXT", big, probe), (int)probe);
    expect_int("write max",
               cfs_write(&fs, "BIGMAX.TXT", big, CFS_MAX_FILE_SIZE),
               (int)CFS_MAX_FILE_SIZE);
    expect_int("write over max",
               cfs_write(&fs, "BIG2.TXT", big, CFS_MAX_FILE_SIZE + 1u),
               CFS_EFBIG);
    free(big);
}

static void test_names(void) {
    BlockDevice dev;
    Cfs fs;
    char n64[65];
    char n65[66];
    int i;

    reset_disk();
    make_dev(&dev);
    expect_int("name format", cfs_format(&dev), CFS_OK);
    expect_int("name mount", cfs_mount(&fs, &dev), CFS_OK);
    for (i = 0; i < 64; i++) {
        n64[i] = 'A';
    }
    n64[64] = 0;
    for (i = 0; i < 65; i++) {
        n65[i] = 'B';
    }
    n65[65] = 0;
    expect_int("name64", cfs_write(&fs, n64, "ok", 2u), 2);
    expect_int("name65", cfs_write(&fs, n65, "ok", 2u), CFS_ENAMETOOLONG);
    expect_int("empty name", cfs_write(&fs, "", "ok", 2u), CFS_EINVAL);
    expect_int("slash name", cfs_write(&fs, "a/b", "ok", 2u), CFS_ENOENT);
}

int main(void) {
    if (test_disk_init() != 0) {
        return 1;
    }
    test_reboot_persist();
    test_disk_full();
    test_io_error();
    test_unknown_magic();
    test_max_file();
    test_names();
    if (g_fails) {
        fprintf(stderr, "%d host cfs tests failed\n", g_fails);
        return 1;
    }
    printf("host cfs tests passed\n");
    return 0;
}
