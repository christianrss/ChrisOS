#include "graphics.h"
#include "gfx_fast.h"
#include "heap.h"
#ifdef __freestanding__
#include "hwgate.h"
#endif

static uint32_t *g_backbuffer;
GfxFramebuffer g_gfx;

typedef struct {
    int x0;
    int y0;
    int x1;
    int y1;
} GfxDirtyRect;

#define GFX_DIRTY_MAX 32

static GfxDirtyRect g_dirty[GFX_DIRTY_MAX];
static int g_dirty_count;

static bool point_in_clip(int x, int y, int clip_x, int clip_y,
                          int clip_w, int clip_h) {
    return clip_w > 0 && clip_h > 0 &&
           x >= clip_x && y >= clip_y &&
           x < clip_x + clip_w && y < clip_y + clip_h;
}

bool gfx_init(uint32_t *address, int width, int height, int pitch_bytes) {
    if (address == 0 || width <= 0 || height <= 0 ||
        width > GFX_MAX_WIDTH || height > GFX_MAX_HEIGHT ||
        pitch_bytes < width * (int)sizeof(uint32_t) ||
        (pitch_bytes % (int)sizeof(uint32_t)) != 0) {
        return false;
    }

    if (g_backbuffer) {
        kfree(g_backbuffer);
        g_backbuffer = 0;
    }
    g_backbuffer = (uint32_t *)kmalloc((uint64_t)width * (uint64_t)height *
                                       sizeof(uint32_t));
    if (!g_backbuffer) {
        return false;
    }

    g_gfx.front = address;
    g_gfx.back = g_backbuffer;
    g_gfx.width = width;
    g_gfx.height = height;
    g_gfx.pitch_pixels = pitch_bytes / (int)sizeof(uint32_t);
    g_dirty_count = 0;
    gfx_mark_dirty(0, 0, width, height);
    return true;
}

uint32_t gfx_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << 16) |
           ((uint32_t)green << 8) |
           (uint32_t)blue;
}

static void put_pixel_raw(int x, int y, uint32_t color) {
    if (!g_gfx.back || x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) {
        return;
    }
    g_gfx.back[y * g_gfx.width + x] = color;
}

void gfx_put_pixel(int x, int y, uint32_t color) {
    put_pixel_raw(x, y, color);
    gfx_mark_dirty(x, y, 1, 1);
}

void gfx_clear(uint32_t color) {
    int x;
    int y;

    if (!g_gfx.back) {
        return;
    }
    for (y = 0; y < g_gfx.height; ++y) {
        uint32_t *row = g_gfx.back + y * g_gfx.width;
        for (x = 0; x < g_gfx.width; ++x) {
            row[x] = color;
        }
    }
    gfx_mark_dirty(0, 0, g_gfx.width, g_gfx.height);
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    int py;

    if (x1 > g_gfx.width) {
        x1 = g_gfx.width;
    }
    if (y1 > g_gfx.height) {
        y1 = g_gfx.height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (py = y0; py < y1; ++py)
        gfx_fast_fill_u32(g_gfx.back + py * g_gfx.width + x0, x1 - x0, color);
    gfx_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_blit_scaled(const uint32_t *src, int sw, int sh,
                     int dx, int dy, int dw, int dh) {
    int x;
    int y;
    int sx;
    int sy;
    int x0;
    int y0;
    int x1;
    int y1;

    if (!src || !g_gfx.back || sw < 1 || sh < 1 || dw < 1 || dh < 1) {
        return;
    }
    x0 = dx < 0 ? 0 : dx;
    y0 = dy < 0 ? 0 : dy;
    x1 = dx + dw;
    y1 = dy + dh;
    if (x1 > g_gfx.width) {
        x1 = g_gfx.width;
    }
    if (y1 > g_gfx.height) {
        y1 = g_gfx.height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    gfx_mark_dirty(x0, y0, x1 - x0, y1 - y0);
    if (dw == sw && dh == sh) {
        int copy_w = x1 - x0;
        int src_x = x0 - dx;
        if (src_x < 0) {
            src_x = 0;
        }
        if (src_x + copy_w > sw) {
            copy_w = sw - src_x;
        }
        if (copy_w < 1) {
            return;
        }
        for (y = y0; y < y1; y++) {
            int src_y = y - dy;
            if (src_y < 0 || src_y >= sh) {
                continue;
            }
            gfx_fast_copy_u32(g_gfx.back + y * g_gfx.width + x0,
                              src + src_y * sw + src_x, copy_w);
        }
        return;
    }
    for (y = y0; y < y1; y++) {
        sy = ((y - dy) * sh) / dh;
        if (sy < 0) {
            sy = 0;
        }
        if (sy >= sh) {
            sy = sh - 1;
        }
        for (x = x0; x < x1; x++) {
            sx = ((x - dx) * sw) / dw;
            if (sx < 0) {
                sx = 0;
            }
            if (sx >= sw) {
                sx = sw - 1;
            }
            g_gfx.back[y * g_gfx.width + x] = src[sy * sw + sx];
        }
    }
}

void gfx_fill_circle(int cx, int cy, int radius, uint32_t color) {
    int x;
    int y;
    int rr;

    if (radius <= 0) {
        return;
    }
    rr = radius * radius;
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= rr) {
                put_pixel_raw(cx + x, cy + y, color);
            }
        }
    }
    gfx_mark_dirty(cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1);
}

int gfx_text_advance(int glyph_width) {
    return glyph_width > 0 ? glyph_width : 1;
}

static void draw_glyph_clipped(GfxFontRowFn font, int glyph_width,
                               int glyph_height, unsigned int character,
                               int x, int y, uint32_t color,
                               int clip_x, int clip_y,
                               int clip_w, int clip_h) {
    int row;
    int col;

    if (font == 0 || glyph_width <= 0 || glyph_width > 32 ||
        glyph_height <= 0) {
        return;
    }

    for (row = 0; row < glyph_height; ++row) {
        uint32_t bits = font(character, row);
        for (col = 0; col < glyph_width; ++col) {
            uint32_t mask = 1u << (unsigned int)(glyph_width - col - 1);
            int px = x + col;
            int py = y + row;
            if ((bits & mask) != 0 &&
                point_in_clip(px, py, clip_x, clip_y, clip_w, clip_h)) {
                put_pixel_raw(px, py, color);
            }
        }
    }
}

void gfx_draw_glyph(GfxFontRowFn font, int glyph_width, int glyph_height,
                    unsigned int character, int x, int y, uint32_t color) {
    draw_glyph_clipped(font, glyph_width, glyph_height, character,
                       x, y, color, 0, 0, g_gfx.width, g_gfx.height);
    gfx_mark_dirty(x, y, glyph_width, glyph_height);
}

void gfx_draw_text_clipped(GfxFontRowFn font, int glyph_width,
                           int glyph_height, const char *text,
                           int x, int y, uint32_t color,
                           int clip_x, int clip_y,
                           int clip_w, int clip_h) {
    int pen_x = x;
    int pen_y = y;
    int advance = gfx_text_advance(glyph_width);

    if (text == 0) {
        return;
    }

    {
        int start_y = y;
        int max_x = x;
    while (*text != '\0') {
        unsigned char ch = (unsigned char)*text++;
        if (ch == '\n') {
            if (pen_x > max_x)
                max_x = pen_x;
            pen_x = x;
            pen_y += glyph_height;
            continue;
        }
        draw_glyph_clipped(font, glyph_width, glyph_height, ch,
                           pen_x, pen_y, color,
                           clip_x, clip_y, clip_w, clip_h);
        pen_x += advance;
    }
        if (pen_x > max_x)
            max_x = pen_x;
        gfx_mark_dirty(x, start_y, max_x - x, pen_y - start_y + glyph_height);
    }
}

void gfx_draw_text(GfxFontRowFn font, int glyph_width, int glyph_height,
                   const char *text, int x, int y, uint32_t color) {
    gfx_draw_text_clipped(font, glyph_width, glyph_height, text,
                          x, y, color, 0, 0,
                          g_gfx.width, g_gfx.height);
}

static void draw_mouse_shape(int x, int y, uint32_t color) {
    static const uint16_t rows[11] = {
        0x7FFu, 0x7FEu, 0x7FCu, 0x7F8u, 0x7F0u, 0x7E0u,
        0x7C0u, 0x780u, 0x700u, 0x600u, 0x400u
    };
    int row;
    int col;

    for (row = 0; row < 11; ++row) {
        for (col = 0; col < 11; ++col) {
            if ((rows[row] & (uint16_t)(1u << (10 - col))) != 0) {
                put_pixel_raw(x + col, y + row, color);
            }
        }
    }
}

void gfx_draw_mouse(int x, int y) {
    draw_mouse_shape(x + 1, y + 1, 0x00000000u);
    draw_mouse_shape(x, y, CHRIS_MOUSE_COLOR);
    gfx_mark_dirty(x, y, 12, 12);
}

static bool rects_touch(const GfxDirtyRect *a, const GfxDirtyRect *b) {
    return a->x0 <= b->x1 && a->x1 >= b->x0 &&
           a->y0 <= b->y1 && a->y1 >= b->y0;
}

void gfx_mark_dirty(int x, int y, int w, int h) {
    GfxDirtyRect rect;
    int i;
    int x1;
    int y1;

    if (w <= 0 || h <= 0)
        return;
    x1 = x + w;
    y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > g_gfx.width)
        x1 = g_gfx.width;
    if (y1 > g_gfx.height)
        y1 = g_gfx.height;
    if (x >= x1 || y >= y1)
        return;
    rect.x0 = x;
    rect.y0 = y;
    rect.x1 = x1;
    rect.y1 = y1;
    i = 0;
    while (i < g_dirty_count) {
        if (rects_touch(&rect, &g_dirty[i])) {
            if (g_dirty[i].x0 < rect.x0)
                rect.x0 = g_dirty[i].x0;
            if (g_dirty[i].y0 < rect.y0)
                rect.y0 = g_dirty[i].y0;
            if (g_dirty[i].x1 > rect.x1)
                rect.x1 = g_dirty[i].x1;
            if (g_dirty[i].y1 > rect.y1)
                rect.y1 = g_dirty[i].y1;
            g_dirty[i] = g_dirty[g_dirty_count - 1];
            --g_dirty_count;
            i = 0;
            continue;
        }
        ++i;
    }
    if (g_dirty_count >= GFX_DIRTY_MAX) {
        g_dirty_count = 1;
        g_dirty[0].x0 = 0;
        g_dirty[0].y0 = 0;
        g_dirty[0].x1 = g_gfx.width;
        g_dirty[0].y1 = g_gfx.height;
        return;
    }
    g_dirty[g_dirty_count++] = rect;
}

void gfx_present(void) {
    int r;
    int y;

    if (!g_gfx.front || !g_gfx.back) {
        return;
    }
    for (r = 0; r < g_dirty_count; ++r) {
        int x0 = g_dirty[r].x0;
        int y0 = g_dirty[r].y0;
        int x1 = g_dirty[r].x1;
        int y1 = g_dirty[r].y1;
        int n = x1 - x0;
        for (y = y0; y < y1; ++y) {
            uint32_t *dst = g_gfx.front + y * g_gfx.pitch_pixels + x0;
            const uint32_t *src = g_gfx.back + y * g_gfx.width + x0;
            gfx_fast_copy_u32(dst, src, n);
        }
    }
    g_dirty_count = 0;
#ifdef __freestanding__
    hw_gpu_flush();
#endif
}
