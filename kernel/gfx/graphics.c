#include "graphics.h"
#include "heap.h"

static uint32_t *g_backbuffer;
GfxFramebuffer g_gfx;

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
    return true;
}

uint32_t gfx_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << 16) |
           ((uint32_t)green << 8) |
           (uint32_t)blue;
}

void gfx_put_pixel(int x, int y, uint32_t color) {
    if (!g_gfx.back || x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) {
        return;
    }
    g_gfx.back[y * g_gfx.width + x] = color;
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
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    int px;
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

    for (py = y0; py < y1; ++py) {
        uint32_t *row = g_gfx.back + py * g_gfx.width;
        for (px = x0; px < x1; ++px) {
            row[px] = color;
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
                gfx_put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

int gfx_text_advance(int glyph_width) {
    int advance = glyph_width - glyph_width / 5;
    return advance > 0 ? advance : 1;
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
                gfx_put_pixel(px, py, color);
            }
        }
    }
}

void gfx_draw_glyph(GfxFontRowFn font, int glyph_width, int glyph_height,
                    unsigned int character, int x, int y, uint32_t color) {
    draw_glyph_clipped(font, glyph_width, glyph_height, character,
                       x, y, color, 0, 0, g_gfx.width, g_gfx.height);
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

    while (*text != '\0') {
        unsigned char ch = (unsigned char)*text++;
        if (ch == '\n') {
            pen_x = x;
            pen_y += glyph_height;
            continue;
        }
        draw_glyph_clipped(font, glyph_width, glyph_height, ch,
                           pen_x, pen_y, color,
                           clip_x, clip_y, clip_w, clip_h);
        pen_x += advance;
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
                gfx_put_pixel(x + col, y + row, color);
            }
        }
    }
}

void gfx_draw_mouse(int x, int y) {
    draw_mouse_shape(x + 1, y + 1, 0x00000000u);
    draw_mouse_shape(x, y, CHRIS_MOUSE_COLOR);
}

void gfx_present(void) {
    int x;
    int y;

    if (!g_gfx.front || !g_gfx.back) {
        return;
    }
    for (y = 0; y < g_gfx.height; ++y) {
        uint32_t *dst = g_gfx.front + y * g_gfx.pitch_pixels;
        const uint32_t *src = g_gfx.back + y * g_gfx.width;
        for (x = 0; x < g_gfx.width; ++x) {
            dst[x] = src[x];
        }
    }
}
