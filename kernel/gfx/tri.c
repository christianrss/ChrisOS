#include "tri.h"

#include "gfx2d.h"
#include "shade.h"
#include "tex.h"
#include "zbuf.h"

static int64_t edge(int ax, int ay, int bx, int by, int px, int py) {
    return (int64_t)(px - ax) * (int64_t)(by - ay) -
           (int64_t)(py - ay) * (int64_t)(bx - ax);
}

static int imin(int a, int b) {
    return a < b ? a : b;
}

static int imax(int a, int b) {
    return a > b ? a : b;
}

static int clip_box(int *minx, int *miny, int *maxx, int *maxy,
                    int w, int h, int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    if (*minx < clip_x0)
        *minx = clip_x0;
    if (*miny < clip_y0)
        *miny = clip_y0;
    if (*maxx >= clip_x1)
        *maxx = clip_x1 - 1;
    if (*maxy >= clip_y1)
        *maxy = clip_y1 - 1;
    if (*minx < 0)
        *minx = 0;
    if (*miny < 0)
        *miny = 0;
    if (*maxx >= w)
        *maxx = w - 1;
    if (*maxy >= h)
        *maxy = h - 1;
    return *minx <= *maxx && *miny <= *maxy;
}

void tri_fill_clip(uint32_t *pixels, int w, int h,
                   int x0, int y0, int32_t z0,
                   int x1, int y1, int32_t z1,
                   int x2, int y2, int32_t z2,
                   int color,
                   int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    int minx;
    int miny;
    int maxx;
    int maxy;
    int y;
    int64_t area;
    uint32_t rgb;
    int64_t col_step0;
    int64_t col_step1;
    int64_t col_step2;

    if (pixels == 0 || w <= 0 || h <= 0)
        return;
    if (color < 0 || color >= GFX2D_PALETTE_SIZE)
        return;

    area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0)
        return;
    if (area < 0) {
        int tx = x1;
        int ty = y1;
        int32_t tz = z1;
        x1 = x2;
        y1 = y2;
        z1 = z2;
        x2 = tx;
        y2 = ty;
        z2 = tz;
        area = -area;
    }

    minx = imin(x0, imin(x1, x2));
    miny = imin(y0, imin(y1, y2));
    maxx = imax(x0, imax(x1, x2));
    maxy = imax(y0, imax(y1, y2));
    if (!clip_box(&minx, &miny, &maxx, &maxy, w, h, clip_x0, clip_y0, clip_x1, clip_y1))
        return;

    rgb = gfx2d_color(color);
    col_step0 = (int64_t)(y2 - y1);
    col_step1 = (int64_t)(y0 - y2);
    col_step2 = (int64_t)(y1 - y0);

    for (y = miny; y <= maxy; ++y) {
        int x;
        int64_t w0 = edge(x1, y1, x2, y2, minx, y);
        int64_t w1 = edge(x2, y2, x0, y0, minx, y);
        int64_t w2 = edge(x0, y0, x1, y1, minx, y);
        uint32_t *row = pixels + y * w;

        for (x = minx; x <= maxx; ++x) {
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                int32_t z;
                uint32_t zu;
                z = (int32_t)((w0 * (int64_t)z0 + w1 * (int64_t)z1 +
                               w2 * (int64_t)z2) / area);
                if (z < 0)
                    z = 0;
                zu = (uint32_t)z;
                if (zbuf_test(x, y, zu))
                    row[x] = rgb;
            }
            w0 += col_step0;
            w1 += col_step1;
            w2 += col_step2;
        }
    }
}

void tri_fill(uint32_t *pixels, int w, int h,
              int x0, int y0, int32_t z0,
              int x1, int y1, int32_t z1,
              int x2, int y2, int32_t z2,
              int color) {
    tri_fill_clip(pixels, w, h,
                  x0, y0, z0, x1, y1, z1, x2, y2, z2, color,
                  0, 0, w, h);
}

void tri_fill_lit(uint32_t *pixels, int w, int h,
                  int x0, int y0, int32_t z0, float u0, float v0,
                  float nx0, float ny0, float nz0,
                  int x1, int y1, int32_t z1, float u1, float v1,
                  float nx1, float ny1, float nz1,
                  int x2, int y2, int32_t z2, float u2, float v2,
                  float nx2, float ny2, float nz2,
                  int color, int texid,
                  int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    int minx;
    int miny;
    int maxx;
    int maxy;
    int y;
    int64_t area;
    uint32_t base;
    int64_t col_step0;
    int64_t col_step1;
    int64_t col_step2;
    float ia;

    if (pixels == 0 || w <= 0 || h <= 0)
        return;

    area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0)
        return;
    if (area < 0) {
        int tx = x1;
        int ty = y1;
        int32_t tz = z1;
        float tu = u1, tv = v1, tnx = nx1, tny = ny1, tnz = nz1;
        x1 = x2; y1 = y2; z1 = z2; u1 = u2; v1 = v2; nx1 = nx2; ny1 = ny2; nz1 = nz2;
        x2 = tx; y2 = ty; z2 = tz; u2 = tu; v2 = tv; nx2 = tnx; ny2 = tny; nz2 = tnz;
        area = -area;
    }

    minx = imin(x0, imin(x1, x2));
    miny = imin(y0, imin(y1, y2));
    maxx = imax(x0, imax(x1, x2));
    maxy = imax(y0, imax(y1, y2));
    if (!clip_box(&minx, &miny, &maxx, &maxy, w, h, clip_x0, clip_y0, clip_x1, clip_y1))
        return;

    if (color >= 0 && color < GFX2D_PALETTE_SIZE)
        base = gfx2d_color(color);
    else
        base = 0xC0C0C0u;
    ia = 1.0f / (float)area;
    col_step0 = (int64_t)(y2 - y1);
    col_step1 = (int64_t)(y0 - y2);
    col_step2 = (int64_t)(y1 - y0);

    for (y = miny; y <= maxy; ++y) {
        int x;
        int64_t w0 = edge(x1, y1, x2, y2, minx, y);
        int64_t w1 = edge(x2, y2, x0, y0, minx, y);
        int64_t w2 = edge(x0, y0, x1, y1, minx, y);
        uint32_t *row = pixels + y * w;

        for (x = minx; x <= maxx; ++x) {
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                int32_t z;
                float bw0 = (float)w0 * ia;
                float bw1 = (float)w1 * ia;
                float bw2 = (float)w2 * ia;
                float u = bw0 * u0 + bw1 * u1 + bw2 * u2;
                float v = bw0 * v0 + bw1 * v1 + bw2 * v2;
                float nx = bw0 * nx0 + bw1 * nx1 + bw2 * nx2;
                float ny = bw0 * ny0 + bw1 * ny1 + bw2 * ny2;
                float nz = bw0 * nz0 + bw1 * nz1 + bw2 * nz2;
                uint32_t rgb;
                z = (int32_t)((w0 * (int64_t)z0 + w1 * (int64_t)z1 +
                               w2 * (int64_t)z2) / area);
                if (z < 0)
                    z = 0;
                if (zbuf_test(x, y, (uint32_t)z)) {
                    if (texid >= 0)
                        rgb = tex_sample(texid, u, v);
                    else
                        rgb = base;
                    row[x] = shade_phong(rgb, nx, ny, nz);
                }
            }
            w0 += col_step0;
            w1 += col_step1;
            w2 += col_step2;
        }
    }
}
