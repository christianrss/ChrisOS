#include "gfx_slot.h"

#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "zbuf.h"

#define GFX_SLOT_MAX 16
#define GFX_SLOT_FULLHD_W 1920
#define GFX_SLOT_FULLHD_H 1080

static struct {
    int used;
    int w;
    int h;
    uint32_t *pixels;
    uint32_t *zbuf;
} g_slots[GFX_SLOT_MAX];

int gfx_slot_alloc(int w, int h, uint32_t **pixels_out, uint32_t **zbuf_out) {
    size_t pix_bytes;
    size_t z_bytes;
    int i;

    if (pixels_out == 0 || zbuf_out == 0 || w <= 0 || h <= 0)
        return -1;
    if (w > GFX_SLOT_FULLHD_W || h > GFX_SLOT_FULLHD_H)
        return -1;
    for (i = 0; i < GFX_SLOT_MAX; ++i) {
        if (!g_slots[i].used)
            break;
    }
    if (i == GFX_SLOT_MAX)
        return -1;
    pix_bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    z_bytes = pix_bytes;
    g_slots[i].pixels = (uint32_t *)kmalloc(pix_bytes);
    g_slots[i].zbuf = (uint32_t *)kmalloc(z_bytes);
    if (g_slots[i].pixels == 0 || g_slots[i].zbuf == 0) {
        if (g_slots[i].pixels != 0)
            kfree(g_slots[i].pixels);
        if (g_slots[i].zbuf != 0)
            kfree(g_slots[i].zbuf);
        g_slots[i].pixels = 0;
        g_slots[i].zbuf = 0;
        return -1;
    }
    {
        size_t k;
        for (k = 0; k < (size_t)w * (size_t)h; ++k)
            g_slots[i].zbuf[k] = ZBUF_FAR;
    }
    g_slots[i].used = 1;
    g_slots[i].w = w;
    g_slots[i].h = h;
    *pixels_out = g_slots[i].pixels;
    *zbuf_out = g_slots[i].zbuf;
    zbuf_set_size(w, h);
    zbuf_bind(g_slots[i].zbuf);
    return i;
}

int gfx_slot_alloc2d(int w, int h, uint32_t **pixels_out) {
    size_t pix_bytes;
    int i;

    if (pixels_out == 0 || w <= 0 || h <= 0)
        return -1;
    if (w > GFX_SLOT_FULLHD_W || h > GFX_SLOT_FULLHD_H)
        return -1;
    for (i = 0; i < GFX_SLOT_MAX; ++i) {
        if (!g_slots[i].used)
            break;
    }
    if (i == GFX_SLOT_MAX)
        return -1;
    pix_bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    g_slots[i].pixels = (uint32_t *)kmalloc(pix_bytes);
    if (g_slots[i].pixels == 0)
        return -1;
    g_slots[i].zbuf = 0;
    g_slots[i].used = 1;
    g_slots[i].w = w;
    g_slots[i].h = h;
    *pixels_out = g_slots[i].pixels;
    return i;
}

int gfx_slot_resize(int slot_id, int w, int h, uint32_t **pixels_out,
                    uint32_t **zbuf_out) {
    size_t pix_bytes;
    uint32_t *pix;
    uint32_t *zb;

    if (pixels_out == 0 || zbuf_out == 0)
        return -1;
    if (slot_id < 0 || slot_id >= GFX_SLOT_MAX || !g_slots[slot_id].used)
        return -1;
    if (w <= 0 || h <= 0 || w > GFX_SLOT_FULLHD_W || h > GFX_SLOT_FULLHD_H)
        return -1;
    if (g_slots[slot_id].w == w && g_slots[slot_id].h == h &&
        g_slots[slot_id].zbuf != 0) {
        *pixels_out = g_slots[slot_id].pixels;
        *zbuf_out = g_slots[slot_id].zbuf;
        zbuf_set_size(w, h);
        zbuf_bind(g_slots[slot_id].zbuf);
        return slot_id;
    }
    pix_bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    pix = (uint32_t *)kmalloc(pix_bytes);
    zb = (uint32_t *)kmalloc(pix_bytes);
    if (pix == 0 || zb == 0) {
        if (pix != 0)
            kfree(pix);
        if (zb != 0)
            kfree(zb);
        return -1;
    }
    {
        size_t k;
        for (k = 0; k < (size_t)w * (size_t)h; ++k)
            zb[k] = ZBUF_FAR;
    }
    kfree(g_slots[slot_id].pixels);
    if (g_slots[slot_id].zbuf != 0)
        kfree(g_slots[slot_id].zbuf);
    g_slots[slot_id].pixels = pix;
    g_slots[slot_id].zbuf = zb;
    g_slots[slot_id].w = w;
    g_slots[slot_id].h = h;
    *pixels_out = pix;
    *zbuf_out = zb;
    zbuf_set_size(w, h);
    zbuf_bind(zb);
    return slot_id;
}

int gfx_slot_resize2d(int slot_id, int w, int h, uint32_t **pixels_out) {
    size_t pix_bytes;
    uint32_t *pix;

    if (pixels_out == 0)
        return -1;
    if (slot_id < 0 || slot_id >= GFX_SLOT_MAX || !g_slots[slot_id].used)
        return -1;
    if (w <= 0 || h <= 0 || w > GFX_SLOT_FULLHD_W || h > GFX_SLOT_FULLHD_H)
        return -1;
    if (g_slots[slot_id].w == w && g_slots[slot_id].h == h) {
        *pixels_out = g_slots[slot_id].pixels;
        return slot_id;
    }
    pix_bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    pix = (uint32_t *)kmalloc(pix_bytes);
    if (pix == 0)
        return -1;
    kfree(g_slots[slot_id].pixels);
    if (g_slots[slot_id].zbuf != 0) {
        kfree(g_slots[slot_id].zbuf);
        g_slots[slot_id].zbuf = 0;
    }
    g_slots[slot_id].pixels = pix;
    g_slots[slot_id].w = w;
    g_slots[slot_id].h = h;
    *pixels_out = pix;
    return slot_id;
}

void gfx_slot_free(int slot_id) {
    if (slot_id < 0 || slot_id >= GFX_SLOT_MAX || !g_slots[slot_id].used)
        return;
    kfree(g_slots[slot_id].pixels);
    if (g_slots[slot_id].zbuf != 0)
        kfree(g_slots[slot_id].zbuf);
    g_slots[slot_id].pixels = 0;
    g_slots[slot_id].zbuf = 0;
    g_slots[slot_id].used = 0;
    zbuf_bind(0);
    zbuf_set_size(320, 200);
}
