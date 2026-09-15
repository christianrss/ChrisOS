#ifndef CHRIS_GRAPHICS_H
#define CHRIS_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#define GFX_MAX_WIDTH  1920
#define GFX_MAX_HEIGHT 1080

#define CHRIS_DESKTOP_COLOR 0x00B5E8FFu
#define CHRIS_TASKBAR_COLOR 0x0000FF00u
#define CHRIS_SHELL_COLOR   0x00000080u
#define CHRIS_BALL_COLOR    0x00808000u
#define CHRIS_EDITOR_COLOR  0x00800000u
#define CHRIS_TITLE_COLOR   0x00208020u
#define CHRIS_WINDOW_COLOR  0x00FFFFFFu
#define CHRIS_TEXT_COLOR    0x00000000u
#define CHRIS_MOUSE_COLOR   0x00EFFFFFu

typedef uint32_t (*GfxFontRowFn)(unsigned int character, int row);

typedef struct {
    uint32_t *front;
    uint32_t *back;
    int width;
    int height;
    int pitch_pixels;
} GfxFramebuffer;

extern GfxFramebuffer g_gfx;

bool gfx_init(uint32_t *address, int width, int height, int pitch_bytes);
uint32_t gfx_rgb(uint8_t red, uint8_t green, uint8_t blue);
void gfx_clear(uint32_t color);
void gfx_put_pixel(int x, int y, uint32_t color);
void gfx_fill_rect(int x, int y, int width, int height, uint32_t color);
void gfx_fill_circle(int cx, int cy, int radius, uint32_t color);
void gfx_draw_glyph(GfxFontRowFn font, int glyph_width, int glyph_height,
                    unsigned int character, int x, int y, uint32_t color);
void gfx_draw_text(GfxFontRowFn font, int glyph_width, int glyph_height,
                   const char *text, int x, int y, uint32_t color);
void gfx_draw_text_clipped(GfxFontRowFn font, int glyph_width, int glyph_height,
                           const char *text, int x, int y, uint32_t color,
                           int clip_x, int clip_y, int clip_w, int clip_h);
int gfx_text_advance(int glyph_width);
void gfx_draw_mouse(int x, int y);
void gfx_present(void);

#endif
