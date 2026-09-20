#include "gfx2d.h"

#include "gfx_fast.h"
#include "zbuf.h"
#ifdef __freestanding__
#include "tile.h"
#endif

const uint32_t gfx2d_palette[GFX2D_PALETTE_SIZE] = {
    0x000000u, 0x000080u, 0x008000u, 0x008080u,
    0x800000u, 0x800080u, 0x808000u, 0xC0C0C0u,
    0x808080u, 0x0000FFu, 0x00FF00u, 0x00FFFFu,
    0xFF0000u, 0xFF00FFu, 0xFFFF00u, 0xFFFFFFu
};

static int area_ok(int rw, int rh) {
    if (rw <= 0 || rh <= 0)
        return 0;
    if (rw > 4096 || rh > 4096)
        return 0;
    if ((int64_t)rw * (int64_t)rh > GFX2D_MAX_AREA)
        return 0;
    return 1;
}

uint32_t gfx2d_color(int index) {
    if (index < 0 || index >= GFX2D_PALETTE_SIZE)
        return gfx2d_palette[0];
    return gfx2d_palette[index];
}

void gfx2d_put(uint32_t *pixels, int w, int h, int x, int y, int color) {
    if (pixels == 0 || w <= 0 || h <= 0)
        return;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;
    if (color < 0 || color >= GFX2D_PALETTE_SIZE)
        return;
    pixels[y * w + x] = gfx2d_palette[color];
}

void gfx2d_clear(uint32_t *pixels, int w, int h, int color) {
    int y;
    uint32_t rgb;

    if (pixels == 0 || w <= 0 || h <= 0)
        return;
    if (color < 0 || color >= GFX2D_PALETTE_SIZE)
        return;
    rgb = gfx2d_palette[color];
#ifdef __freestanding__
    if (w * h >= 512 * 512) {
        tile_parallel_clear(pixels, w, h, rgb);
        zbuf_set_size(w, h);
        zbuf_clear();
        return;
    }
#endif
    for (y = 0; y < h; ++y)
        gfx_fast_fill_u32(pixels + y * w, w, rgb);
    zbuf_set_size(w, h);
    zbuf_clear();
}

void gfx2d_fill(uint32_t *pixels, int w, int h, int x, int y, int rw, int rh,
                int color) {
    int x0;
    int y0;
    int x1;
    int y1;
    int px;
    int py;
    uint32_t rgb;

    if (pixels == 0 || w <= 0 || h <= 0)
        return;
    if (!area_ok(rw, rh))
        return;
    if (color < 0 || color >= GFX2D_PALETTE_SIZE)
        return;

    x0 = x;
    y0 = y;
    x1 = x + rw;
    y1 = y + rh;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    if (y1 > h)
        y1 = h;
    if (x0 >= x1 || y0 >= y1)
        return;

    rgb = gfx2d_palette[color];
    for (py = y0; py < y1; ++py) {
        uint32_t *row = pixels + py * w;
        for (px = x0; px < x1; ++px)
            row[px] = rgb;
    }
}

void gfx2d_line(uint32_t *pixels, int w, int h, int x0, int y0, int x1, int y1,
                int color) {
    int dx;
    int dy;
    int sx;
    int sy;
    int err;
    int x;
    int y;

    if (pixels == 0 || w <= 0 || h <= 0)
        return;
    if (color < 0 || color >= GFX2D_PALETTE_SIZE)
        return;

    x = x0;
    y = y0;
    dx = x1 - x0;
    if (dx < 0)
        dx = -dx;
    dy = y1 - y0;
    if (dy < 0)
        dy = -dy;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;

    for (;;) {
        gfx2d_put(pixels, w, h, x, y, color);
        if (x == x1 && y == y1)
            break;
        {
            int e2 = err * 2;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }
        }
    }
}

void gfx2d_sprite(uint32_t *pixels, int w, int h, const uint8_t *src,
                  int x, int y, int sw, int sh, int key) {
    int row;
    int col;

    if (pixels == 0 || src == 0 || w <= 0 || h <= 0)
        return;
    if (!area_ok(sw, sh))
        return;

    for (row = 0; row < sh; ++row) {
        for (col = 0; col < sw; ++col) {
            int px = x + col;
            int py = y + row;
            int index = (int)src[row * sw + col];
            if (index == key)
                continue;
            gfx2d_put(pixels, w, h, px, py, index);
        }
    }
}

void gfx2d_tilemap(uint32_t *pixels, int w, int h, const uint8_t *data,
                   int mapw, int maph, int tw, int th, int ox, int oy) {
    int atlas_bytes;
    int map_bytes;
    const uint8_t *atlas;
    const uint8_t *map;
    int ty;
    int tx;

    if (pixels == 0 || data == 0 || w <= 0 || h <= 0)
        return;
    if (mapw <= 0 || maph <= 0 || tw <= 0 || th <= 0)
        return;
    if (mapw > 256 || maph > 256 || tw > 64 || th > 64)
        return;
    if ((int64_t)mapw * (int64_t)maph * (int64_t)tw * (int64_t)th > GFX2D_MAX_AREA)
        return;
    if (!area_ok(tw, th))
        return;

    atlas_bytes = GFX2D_ATLAS_TILES * tw * th;
    map_bytes = mapw * maph;
    if (atlas_bytes <= 0 || map_bytes <= 0)
        return;

    atlas = data;
    map = data + atlas_bytes;

    for (ty = 0; ty < maph; ++ty) {
        for (tx = 0; tx < mapw; ++tx) {
            int tile = (int)map[ty * mapw + tx];
            const uint8_t *src;
            if (tile < 0 || tile >= GFX2D_ATLAS_TILES)
                continue;
            src = atlas + tile * tw * th;
            gfx2d_sprite(pixels, w, h, src, ox + tx * tw, oy + ty * th,
                         tw, th, -1);
        }
    }
}
