/* LEARN:ACL64-03 */
#include "cfs.h"
#include "cfs_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_f;

static int fread_dev(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    (void)ctx;
    if (fseek(g_f, (long)lba * 512, SEEK_SET) != 0) return BD_EIO;
    if (fread(dst, 512, count, g_f) != count) return BD_EIO;
    return BD_OK;
}

static int fwrite_dev(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    (void)ctx;
    if (fseek(g_f, (long)lba * 512, SEEK_SET) != 0) return BD_EIO;
    if (fwrite(src, 512, count, g_f) != count) return BD_EIO;
    return BD_OK;
}

static int fflush_dev(void *ctx) {
    (void)ctx;
    return fflush(g_f) == 0 ? BD_OK : BD_EIO;
}

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    unsigned long mode;
    if (argc < 4) {
        fprintf(stderr, "usage: %s disk.img path mode\n", argv[0]);
        return 1;
    }
    mode = strtoul(argv[3], 0, 0);
    if (mode > CFS_PERM_ALL) {
        fprintf(stderr, "mode must be 0..%u\n", (unsigned)CFS_PERM_ALL);
        return 1;
    }
    g_f = fopen(argv[1], "r+b");
    if (!g_f) return 2;
    memset(&d, 0, sizeof(d));
    d.sector_size = 512;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = fread_dev;
    d.write = fwrite_dev;
    d.flush = fflush_dev;
    d.writable = 1;
    if (cfs_mount(&fs, &d) != CFS_OK) {
        fclose(g_f);
        return 3;
    }
    if (cfs_chmod(&fs, argv[2], (uint32_t)mode) != CFS_OK) {
        fclose(g_f);
        return 4;
    }
    if (cfs_sync(&fs) != CFS_OK) {
        fclose(g_f);
        return 5;
    }
    fclose(g_f);
    puts("cfs_chmod: ok");
    return 0;
}
