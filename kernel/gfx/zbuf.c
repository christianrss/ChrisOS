#include "zbuf.h"

#include "gfx_fast.h"

#define ZBUF_STATIC_W 1024
#define ZBUF_STATIC_H 768

static uint32_t g_zbuf_static[ZBUF_STATIC_W * ZBUF_STATIC_H];
static uint32_t *g_zbuf = g_zbuf_static;
static int g_w = 320;
static int g_h = 200;

void zbuf_bind(uint32_t *external) {
    g_zbuf = external ? external : g_zbuf_static;
}

void zbuf_set_size(int width, int height) {
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (width > ZBUF_MAX_W)
        width = ZBUF_MAX_W;
    if (height > ZBUF_MAX_H)
        height = ZBUF_MAX_H;
    g_w = width;
    g_h = height;
    if (g_zbuf == g_zbuf_static &&
        (width > ZBUF_STATIC_W || height > ZBUF_STATIC_H ||
         width * height > ZBUF_STATIC_W * ZBUF_STATIC_H)) {
        g_zbuf = 0;
    }
}

int zbuf_width(void) {
    return g_w;
}

int zbuf_height(void) {
    return g_h;
}

void zbuf_clear(void) {
    if (!g_zbuf)
        return;
    gfx_fast_fill_u32(g_zbuf, g_w * g_h, ZBUF_FAR);
}

int zbuf_test(int x, int y, uint32_t z) {
    uint32_t *cell;
    if (!g_zbuf || x < 0 || y < 0 || x >= g_w || y >= g_h)
        return 0;
    cell = &g_zbuf[y * g_w + x];
    if (z < *cell) {
        *cell = z;
        return 1;
    }
    return 0;
}
