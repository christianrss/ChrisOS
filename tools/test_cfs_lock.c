#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"
#include "test_disk.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static __thread uint32_t tls_holder = 1;

uint32_t fs_lock_holder(void) {
    return tls_holder;
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

static Cfs g_fs;
static int g_fail;

static void *writer(void *arg) {
    int i;
    (void)arg;
    tls_holder = 2;
    for (i = 0; i < 40; ++i) {
        const char *body = (i & 1) ? "AAAAAAAA" : "BBBBBBBB";
        int n = cfs_write(&g_fs, "T.TXT", body, 8u);
        if (n != 8) {
            g_fail = 1;
        }
    }
    return 0;
}

static void *reader(void *arg) {
    int i;
    (void)arg;
    tls_holder = 3;
    for (i = 0; i < 40; ++i) {
        char got[8];
        int n = cfs_read(&g_fs, "T.TXT", got, 8u);
        int k;
        if (n == CFS_ENOENT) {
            continue;
        }
        if (n != 8) {
            g_fail = 1;
            continue;
        }
        for (k = 1; k < 8; ++k) {
            if (got[k] != got[0]) {
                g_fail = 1;
            }
        }
        if (got[0] != 'A' && got[0] != 'B') {
            g_fail = 1;
        }
    }
    return 0;
}

int main(void) {
    BlockDevice dev;
    pthread_t tw;
    pthread_t tr;
    char got[8];
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
    if (cfs_format(&dev) != CFS_OK || cfs_mount(&g_fs, &dev) != CFS_OK) {
        fprintf(stderr, "fail: mount\n");
        return 1;
    }
    if (pthread_create(&tw, 0, writer, 0) != 0 ||
        pthread_create(&tr, 0, reader, 0) != 0) {
        fprintf(stderr, "fail: thread\n");
        return 1;
    }
    pthread_join(tw, 0);
    pthread_join(tr, 0);
    if (g_fail || cfs_read(&g_fs, "T.TXT", got, 8u) != 8) {
        fprintf(stderr, "fail: torn or missing\n");
        return 1;
    }
    if (cfs_fsck(&g_fs) != CFS_OK) {
        fprintf(stderr, "fail: fsck\n");
        return 1;
    }
    puts("test_cfs_lock: ok");
    return 0;
}
