#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/gfx2d.h"

#define TW 32
#define TH 24

static uint32_t fb[TW * TH];

static uint32_t at(int x, int y) {
    return fb[y * TW + x];
}

int main(void) {
    static uint8_t sprite[16] = {
        12, 12, 0, 0,
        12, 15, 0, 0,
        0,  0,  0, 0,
        0,  0,  0, 0
    };
    static uint8_t tiles[16 * 2 * 2 + 2 * 2];
    int i;

    memset(fb, 0xAA, sizeof(fb));
    gfx2d_clear(fb, TW, TH, 1);
    assert(at(0, 0) == 0x000080u);
    assert(at(TW - 1, TH - 1) == 0x000080u);

    gfx2d_put(fb, TW, TH, 10, 10, 12);
    assert(at(10, 10) == 0xFF0000u);

    gfx2d_put(fb, TW, TH, -1, 0, 15);
    gfx2d_put(fb, TW, TH, 0, -1, 15);
    gfx2d_put(fb, TW, TH, TW, 0, 15);
    gfx2d_put(fb, TW, TH, 0, TH, 15);
    gfx2d_put(fb, TW, TH, 3, 3, 99);
    assert(at(0, 0) == 0x000080u);
    assert(at(3, 3) == 0x000080u);

    gfx2d_fill(fb, TW, TH, 20, 8, 4, 3, 10);
    assert(at(20, 8) == 0x00FF00u);
    assert(at(23, 10) == 0x00FF00u);
    assert(at(24, 8) == 0x000080u);
    assert(at(20, 11) == 0x000080u);

    gfx2d_fill(fb, TW, TH, -2, -2, 4, 4, 14);
    assert(at(0, 0) == 0xFFFF00u);
    assert(at(1, 1) == 0xFFFF00u);
    assert(at(2, 2) == 0x000080u);

    gfx2d_line(fb, TW, TH, 0, 20, 3, 20, 15);
    assert(at(0, 20) == 0xFFFFFFu);
    assert(at(1, 20) == 0xFFFFFFu);
    assert(at(2, 20) == 0xFFFFFFu);
    assert(at(3, 20) == 0xFFFFFFu);

    gfx2d_line(fb, TW, TH, 5, 5, 8, 8, 11);
    assert(at(5, 5) == 0x00FFFFu);
    assert(at(6, 6) == 0x00FFFFu);
    assert(at(7, 7) == 0x00FFFFu);
    assert(at(8, 8) == 0x00FFFFu);

    gfx2d_sprite(fb, TW, TH, sprite, 2, 12, 4, 4, 0);
    assert(at(2, 12) == 0xFF0000u);
    assert(at(3, 12) == 0xFF0000u);
    assert(at(3, 13) == 0xFFFFFFu);
    assert(at(4, 12) == 0x000080u);

    for (i = 0; i < 16 * 2 * 2 + 4; ++i)
        tiles[i] = 0;
    tiles[0] = 9;
    tiles[1] = 9;
    tiles[2] = 9;
    tiles[3] = 9;
    tiles[16 * 4 + 0] = 0;
    tiles[16 * 4 + 1] = 1;
    tiles[16 * 4 + 2] = 0;
    tiles[16 * 4 + 3] = 1;
    gfx2d_clear(fb, TW, TH, 0);
    gfx2d_tilemap(fb, TW, TH, tiles, 2, 2, 2, 2, 0, 0);
    assert(at(0, 0) == 0x0000FFu);
    assert(at(1, 1) == 0x0000FFu);
    assert(at(2, 0) == 0x000000u);

    puts("test_gfx2d: ok");
    return 0;
}
