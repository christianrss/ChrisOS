/* test_block_device.c */
#include "block_device.h"

static unsigned char disk[4][512];

static int rd(void *ctx, uint32_t lba, uint32_t n, void *dst) {
    unsigned char *out = dst;
    unsigned char (*p)[512] = ctx;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 512; j++)
            out[i * 512u + j] = p[lba + i][j];
    return 0;
}

static int wr(void *ctx, uint32_t lba, uint32_t n, const void *src) {
    const unsigned char *in = src;
    unsigned char (*p)[512] = ctx;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 512; j++)
            p[lba + i][j] = in[i * 512u + j];
    return 0;
}

int main(void) {
    BlockDevice d = { disk, 512u, 4u, rd, wr, 0, 1u };
    unsigned char a[512], b[512];
    uint32_t i;
    for (i = 0; i < 512; i++) a[i] = (unsigned char)i;
    if (bd_write(&d, 3, 1, a) != 0) return 1;
    if (bd_read(&d, 3, 1, b) != 0) return 2;
    for (i = 0; i < 512; i++) if (a[i] != b[i]) return 3;
    if (bd_read(&d, 4, 1, b) != BD_ERANGE) return 4;
    if (bd_read(&d, 0, 0, b) != BD_ERANGE) return 5;
    return 0;
}
