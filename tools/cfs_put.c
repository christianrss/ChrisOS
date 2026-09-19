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
    static unsigned char buf[70000];
    uint32_t i;
    if (argc < 2) return 1;
    g_f = fopen(argv[1], "r+b");
    if (!g_f) return 2;
    memset(&d, 0, sizeof(d));
    d.sector_size = 512;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = fread_dev;
    d.write = fwrite_dev;
    d.flush = fflush_dev;
    d.writable = 1;
    if (cfs_mount(&fs, &d) != CFS_OK) return 3;
    for (i = 0; i < 70000u; i++) buf[i] = (unsigned char)(i & 0xFFu);
    if (cfs_write(&fs, "GAMES/BIG.DAT", buf, 70000u) != 70000) return 4;
    if (cfs_sync(&fs) != CFS_OK) return 5;
    fclose(g_f);
    puts("cfs_put: ok");
    return 0;
}
