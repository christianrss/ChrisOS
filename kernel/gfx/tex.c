#include "tex.h"

static uint32_t g_atlas[TEX_SLOTS][TEX_SIZE * TEX_SIZE];
static int g_slot = 1;
static int g_ready;
static float g_ofs_u;
static float g_ofs_v;

static uint32_t rgb(int r, int g, int b) {
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fill_noise(int slot, int r, int g, int b, int var) {
    int y;
    int x;
    unsigned s = (unsigned)(slot * 1103515245u + 12345u);
    for (y = 0; y < TEX_SIZE; ++y) {
        for (x = 0; x < TEX_SIZE; ++x) {
            int n;
            s = s * 1664525u + 1013904223u;
            n = (int)(s % (unsigned)(var * 2 + 1)) - var;
            g_atlas[slot][y * TEX_SIZE + x] = rgb(r + n, g + n, b + n);
        }
    }
}

void tex_init(void) {
    int i;
    if (g_ready)
        return;
    fill_noise(0, 20, 20, 24, 4);
    fill_noise(1, 48, 140, 42, 18);
    fill_noise(2, 110, 72, 38, 14);
    fill_noise(3, 96, 96, 104, 12);
    fill_noise(4, 140, 98, 42, 10);
    fill_noise(5, 40, 70, 160, 10);
    fill_noise(6, 180, 50, 40, 12);
    fill_noise(7, 200, 180, 40, 10);
    for (i = 8; i < TEX_SLOTS; ++i)
        fill_noise(i, 40 + i * 8, 40, 80, 10);
    g_ready = 1;
}

void tex_state_save(TexState *out) {
    if (!out) {
        return;
    }
    out->slot = g_slot;
    out->du = g_ofs_u;
    out->dv = g_ofs_v;
}

void tex_state_load(const TexState *in) {
    if (!in) {
        return;
    }
    g_slot = in->slot;
    g_ofs_u = in->du;
    g_ofs_v = in->dv;
}

void tex_set_slot(int slot) {
    tex_init();
    if (slot < 0)
        slot = 0;
    if (slot >= TEX_SLOTS)
        slot = TEX_SLOTS - 1;
    g_slot = slot;
}

int tex_slot(void) {
    return g_slot;
}

void tex_ofs(float du, float dv) {
    g_ofs_u = du;
    g_ofs_v = dv;
}

uint32_t tex_sample(int slot, float u, float v) {
    int x;
    int y;
    tex_init();
    if (slot < 0 || slot >= TEX_SLOTS)
        slot = g_slot;
    if (slot == 5) {
        u += g_ofs_u;
        v += g_ofs_v;
    }
    while (u < 0.0f)
        u += 1.0f;
    while (u >= 1.0f)
        u -= 1.0f;
    while (v < 0.0f)
        v += 1.0f;
    while (v >= 1.0f)
        v -= 1.0f;
    x = (int)(u * (float)TEX_SIZE);
    y = (int)(v * (float)TEX_SIZE);
    if (x < 0)
        x = 0;
    if (x >= TEX_SIZE)
        x = TEX_SIZE - 1;
    if (y < 0)
        y = 0;
    if (y >= TEX_SIZE)
        y = TEX_SIZE - 1;
    return g_atlas[slot][y * TEX_SIZE + x];
}
