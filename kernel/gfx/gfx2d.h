#ifndef CHRIS_GFX2D_H
#define CHRIS_GFX2D_H

#include <stdint.h>

#define GFX2D_PALETTE_SIZE 16
#define GFX2D_ATLAS_TILES 16
#define GFX2D_MAX_AREA (4096 * 4096)

extern const uint32_t gfx2d_palette[GFX2D_PALETTE_SIZE];

uint32_t gfx2d_color(int index);
void gfx2d_clear(uint32_t *pixels, int w, int h, int color);
void gfx2d_put(uint32_t *pixels, int w, int h, int x, int y, int color);
void gfx2d_fill(uint32_t *pixels, int w, int h, int x, int y, int rw, int rh,
                int color);
void gfx2d_line(uint32_t *pixels, int w, int h, int x0, int y0, int x1, int y1,
                int color);
void gfx2d_sprite(uint32_t *pixels, int w, int h, const uint8_t *src,
                  int x, int y, int sw, int sh, int key);
void gfx2d_tilemap(uint32_t *pixels, int w, int h, const uint8_t *data,
                   int mapw, int maph, int tw, int th, int ox, int oy);
void gfx2d_layer(uint32_t *dst, int dw, int dh, const uint32_t *src, int sw,
                 int sh, int camx, int camy);

#endif
