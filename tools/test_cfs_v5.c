/* v4 images of 512 MiB still mount. v5 follows sector_count past that size. */
#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"

#include <stdio.h>
#include <string.h>

#define SLOT_N 4096

typedef struct Slot {
    uint32_t lba;
    uint8_t data[STOR_SECTOR_SIZE];
    int used;
} Slot;

typedef struct Disk {
    Slot slots[SLOT_N];
    int nslot;
    uint32_t count;
    uint32_t hi;
    int track;
} Disk;

static int g_fails;

static int slot_find(Disk *d, uint32_t lba) {
    int i;
    for (i = 0; i < d->nslot; i++) {
        if (d->slots[i].used && d->slots[i].lba == lba)
            return i;
    }
    return -1;
}

static int dev_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    Disk *d = ctx;
    uint8_t *out = dst;
    uint32_t s;
    if (lba >= d->count || count > d->count - lba)
        return BD_ERANGE;
    for (s = 0; s < count; s++) {
        int id = slot_find(d, lba + s);
        if (id < 0)
            memset(out + s * STOR_SECTOR_SIZE, 0, STOR_SECTOR_SIZE);
        else
            memcpy(out + s * STOR_SECTOR_SIZE, d->slots[id].data,
                   STOR_SECTOR_SIZE);
    }
    return BD_OK;
}

static int dev_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    Disk *d = ctx;
    const uint8_t *in = src;
    uint32_t s;
    if (lba >= d->count || count > d->count - lba)
        return BD_ERANGE;
    for (s = 0; s < count; s++) {
        int id = slot_find(d, lba + s);
        if (id < 0) {
            if (d->nslot >= SLOT_N) {
                fprintf(stderr, "sparse disk full at lba %u\n", lba + s);
                return BD_EIO;
            }
            id = d->nslot++;
            d->slots[id].used = 1;
            d->slots[id].lba = lba + s;
        }
        memcpy(d->slots[id].data, in + s * STOR_SECTOR_SIZE, STOR_SECTOR_SIZE);
        if (d->track && lba + s > d->hi)
            d->hi = lba + s;
    }
    return BD_OK;
}

static void bind(BlockDevice *dev, Disk *disk, uint32_t sectors) {
    memset(disk, 0, sizeof(*disk));
    disk->count = sectors;
    memset(dev, 0, sizeof(*dev));
    dev->ctx = disk;
    dev->sector_size = STOR_SECTOR_SIZE;
    dev->sector_count = sectors;
    dev->read = dev_read;
    dev->write = dev_write;
    dev->writable = 1;
}

static void expect_int(const char *name, int got, int want) {
    if (got == want)
        return;
    fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
    g_fails++;
}

static void mark_below(Disk *disk, uint32_t index) {
    uint8_t sec[STOR_SECTOR_SIZE];
    uint32_t bits_per = STOR_SECTOR_SIZE * 8u;
    uint32_t full = index / bits_per;
    uint32_t rem = index % bits_per;
    uint32_t i;
    uint32_t b;
    memset(sec, 0xff, sizeof(sec));
    for (i = 0; i < full; i++) {
        if (dev_write(disk, CFS_BITMAP_LBA + i, 1u, sec) != BD_OK) {
            fprintf(stderr, "bitmap write failed\n");
            g_fails++;
            return;
        }
    }
    if (rem == 0u)
        return;
    memset(sec, 0, sizeof(sec));
    for (b = 0; b < rem; b++)
        sec[b / 8u] |= (uint8_t)(1u << (b % 8u));
    if (dev_write(disk, CFS_BITMAP_LBA + full, 1u, sec) != BD_OK) {
        fprintf(stderr, "bitmap tail write failed\n");
        g_fails++;
    }
}

static void test_legacy_v4(void) {
    Disk disk;
    BlockDevice dev;
    Cfs fs;
    uint8_t sec[STOR_SECTOR_SIZE];
    uint8_t back[8];
    int n;

    bind(&dev, &disk, STOR_DISK_SECTORS);
    expect_int("v4 format", cfs_format(&dev), CFS_OK);
    expect_int("v4 super read", bd_read(&dev, 0, 1, sec), BD_OK);
    expect_int("v5 data lba", (int)cfs_get32(sec + 32), (int)CFS_DATA_LBA);
    expect_int("v5 total", (int)cfs_get32(sec + 8), (int)STOR_DISK_SECTORS);
    sec[4] = (uint8_t)CFS_VERSION_V4;
    sec[5] = 0;
    cfs_put32(sec + 60, cfs_checksum(sec, 60u));
    expect_int("v4 super write", bd_write(&dev, 0, 1, sec), BD_OK);
    expect_int("v4 mount", cfs_mount(&fs, &dev), CFS_OK);
    expect_int("v4 version", (int)fs.super.version, (int)CFS_VERSION_V4);
    expect_int("v4 fsck", cfs_fsck(&fs), CFS_OK);
    n = cfs_write(&fs, "OLD.TXT", "v4", 2u);
    expect_int("v4 write", n, 2);
    n = cfs_read(&fs, "OLD.TXT", back, sizeof(back));
    expect_int("v4 read", n, 2);
    if (n == 2 && (back[0] != 'v' || back[1] != '4')) {
        fprintf(stderr, "FAIL v4 payload\n");
        g_fails++;
    }
}

static void test_larger(void) {
    Disk disk;
    BlockDevice dev;
    Cfs fs;
    CfsSuper geom;
    uint8_t sec[STOR_SECTOR_SIZE];
    uint32_t total = STOR_DISK_SECTORS + 8192u;
    uint32_t index;
    int n;

    memset(&geom, 0, sizeof(geom));
    expect_int("geom", cfs_geom_for_count(total, &geom), 0);
    if (geom.data_lba + geom.data_sectors != total) {
        fprintf(stderr, "FAIL geom end\n");
        g_fails++;
    }
    bind(&dev, &disk, total);
    expect_int("v5 format", cfs_format(&dev), CFS_OK);
    expect_int("v5 super read", bd_read(&dev, 0, 1, sec), BD_OK);
    expect_int("v5 version", (int)cfs_get16(sec + 4), (int)CFS_VERSION);
    expect_int("v5 stored total", (int)cfs_get32(sec + 8), (int)total);
    expect_int("v5 data past old end",
               (int)(cfs_get32(sec + 32) + cfs_get32(sec + 36) > STOR_DISK_SECTORS),
               1);
    index = STOR_DISK_SECTORS - geom.data_lba;
    if (index >= geom.data_sectors) {
        fprintf(stderr, "FAIL no block past old data end\n");
        g_fails++;
        return;
    }
    mark_below(&disk, index);
    expect_int("v5 mount", cfs_mount(&fs, &dev), CFS_OK);
    disk.hi = 0;
    disk.track = 1;
    n = cfs_write(&fs, "FAR.TXT", "x", 1u);
    expect_int("v5 write", n, 1);
    if (disk.hi < STOR_DISK_SECTORS) {
        fprintf(stderr, "FAIL alloc lba %u is inside the old disk\n", disk.hi);
        g_fails++;
    }
}

int main(void) {
    test_legacy_v4();
    test_larger();
    if (g_fails) {
        fprintf(stderr, "test_cfs_v5: %d failure(s)\n", g_fails);
        return 1;
    }
    puts("test_cfs_v5: ok");
    return 0;
}
