#include "cfs.h"
#include "cfs_format.h"
#include <stdio.h>
#include <string.h>

static FILE *g_f;
static int rd(void *c, uint32_t l, uint32_t n, void *d) {
    (void)c;
    if (fseek(g_f, (long)l * 512, SEEK_SET))
        return BD_EIO;
    return fread(d, 512, n, g_f) == n ? BD_OK : BD_EIO;
}
static int wr(void *c, uint32_t l, uint32_t n, const void *s) {
    (void)c;
    if (fseek(g_f, (long)l * 512, SEEK_SET))
        return BD_EIO;
    return fwrite(s, 512, n, g_f) == n ? BD_OK : BD_EIO;
}
static int fl(void *c) {
    (void)c;
    return fflush(g_f) == 0 ? BD_OK : BD_EIO;
}

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    unsigned char b[16];
    uint32_t sz;
    uint16_t ty;
    const char *img = argc > 1 ? argv[1] : "build/disk.img";

    g_f = fopen(img, "r+b");
    if (!g_f) {
        perror(img);
        return 1;
    }
    memset(&d, 0, sizeof(d));
    d.sector_size = 512;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = rd;
    d.write = wr;
    d.flush = fl;
    d.writable = 1;
    if (cfs_mount(&fs, &d) != CFS_OK) {
        printf("mount fail\n");
        return 1;
    }
    if (cfs_stat(&fs, "GAMES/DOOM/DOOM1.WAD", &sz, &ty) != CFS_OK) {
        printf("missing DOOM1.WAD\n");
        return 2;
    }
    if (cfs_read(&fs, "GAMES/DOOM/DOOM1.WAD", b, 12) < 12 ||
        b[0] != 'I' || b[1] != 'W' || b[2] != 'A' || b[3] != 'D') {
        printf("bad IWAD magic\n");
        return 3;
    }
    printf("DOOM1.WAD ok size=%u\n", sz);
    if (cfs_stat(&fs, "GAMES/DOOM/ENGINE.CLV", &sz, &ty) != CFS_OK) {
        printf("missing ENGINE.CLV\n");
        return 4;
    }
    printf("ENGINE.CLV ok size=%u\n", sz);
    return 0;
}
