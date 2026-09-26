#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics.h"

void *kmalloc(uint64_t size) {
    return malloc((size_t)size);
}

void kfree(void *ptr) {
    free(ptr);
}

static int expect_pixel(uint32_t actual, uint32_t expected, const char *what) {
    if (actual == expected)
        return 1;
    fprintf(stderr, "%s: got=%08x want=%08x\n", what,
            (unsigned)actual, (unsigned)expected);
    return 0;
}

int main(void) {
    uint32_t front[18];
    uint32_t wide[160];
    int i;

    for (i = 0; i < 18; ++i)
        front[i] = 0xdeadbeefu;
    if (!gfx_init(front, 4, 3, 6 * (int)sizeof(uint32_t)))
        return 1;

    gfx_clear(0x00112233u);
    gfx_mark_dirty(1, 1, 1, 1);
    gfx_present();
    for (i = 0; i < 18; ++i) {
        int x = i % 6;
        uint32_t want = x < 4 ? 0x00112233u : 0xdeadbeefu;
        if (!expect_pixel(front[i], want, "full clear/pitch"))
            return 1;
    }

    gfx_fill_rect(1, 1, 2, 1, 0x00445566u);
    gfx_present();
    if (!expect_pixel(front[7], 0x00445566u, "partial left") ||
        !expect_pixel(front[8], 0x00445566u, "partial right") ||
        !expect_pixel(front[6], 0x00112233u, "partial neighbor"))
        return 1;

    g_gfx.back[0] = 0x00abcdefu;
    gfx_present();
    if (!expect_pixel(front[0], 0x00112233u, "no damage means no copy"))
        return 1;

    memset(wide, 0, sizeof(wide));
    if (!gfx_init(wide, 80, 2, 80 * (int)sizeof(uint32_t)))
        return 1;
    gfx_clear(0);
    gfx_present();
    for (i = 0; i < 33; ++i)
        gfx_put_pixel(i * 2, 0, 0x00000001u + (uint32_t)i);
    g_gfx.back[159] = 0x00765432u;
    gfx_present();
    if (!expect_pixel(wide[159], 0x00765432u, "damage overflow fallback"))
        return 1;

    {
        uint32_t src[4];
        uint32_t dest[16];
        src[0] = 0x00000011u;
        src[1] = 0x00000022u;
        src[2] = 0x00000033u;
        src[3] = 0x00000044u;
        memset(dest, 0, sizeof(dest));
        if (!gfx_init(dest, 4, 4, 4 * (int)sizeof(uint32_t)))
            return 1;
        gfx_clear(0);
        gfx_present();
        gfx_blit_scaled(src, 2, 2, 0, 0, 4, 4);
        gfx_present();
        if (!expect_pixel(dest[0], 0x00000011u, "scale 00") ||
            !expect_pixel(dest[1], 0x00000011u, "scale 01") ||
            !expect_pixel(dest[2], 0x00000022u, "scale 02") ||
            !expect_pixel(dest[4], 0x00000011u, "scale 10") ||
            !expect_pixel(dest[10], 0x00000044u, "scale 22"))
            return 1;
    }

    puts("test_graphics_present: ok");
    return 0;
}
