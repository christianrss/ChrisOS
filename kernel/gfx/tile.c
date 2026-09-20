#include "tile.h"
#include "gfx_fast.h"
#include "job.h"
#include "tri.h"
#include "tri_bin.h"

typedef struct {
    uint32_t *dest;
    int w;
    int h;
    int tx;
    int ty;
    uint32_t color;
} TileClearArg;

typedef struct {
    uint32_t *pixels;
    int w;
    int h;
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    int x0;
    int y0;
    int32_t z0;
    int x1;
    int y1;
    int32_t z1;
    int x2;
    int y2;
    int32_t z2;
    int color;
} TileTriArg;

static void tile_clear_job(void *arg, uint32_t cpu) {
    TileClearArg *a = (TileClearArg *)arg;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    (void)cpu;
    x0 = a->tx * TILE_SIZE;
    y0 = a->ty * TILE_SIZE;
    x1 = x0 + TILE_SIZE;
    y1 = y0 + TILE_SIZE;
    if (x1 > a->w)
        x1 = a->w;
    if (y1 > a->h)
        y1 = a->h;
    for (y = y0; y < y1; ++y) {
        uint32_t *row = a->dest + y * a->w + x0;
        gfx_fast_fill_u32(row, x1 - x0, a->color);
    }
}

static void tile_tri_job(void *arg, uint32_t cpu) {
    TileTriArg *a = (TileTriArg *)arg;

    (void)cpu;
    tri_fill_clip(a->pixels, a->w, a->h,
                  a->x0, a->y0, a->z0,
                  a->x1, a->y1, a->z1,
                  a->x2, a->y2, a->z2,
                  a->color,
                  a->clip_x0, a->clip_y0, a->clip_x1, a->clip_y1);
}

void tile_parallel_clear(uint32_t *dest, int w, int h, uint32_t color) {
    int txs = (w + TILE_SIZE - 1) / TILE_SIZE;
    int tys = (h + TILE_SIZE - 1) / TILE_SIZE;
    int ty;
    int tx;
    static TileClearArg args[JOB_QUEUE_CAP];
    int n = 0;

    for (ty = 0; ty < tys; ++ty) {
        for (tx = 0; tx < txs; ++tx) {
            if (n >= (int)JOB_QUEUE_CAP) {
                job_wait_idle();
                n = 0;
            }
            args[n].dest = dest;
            args[n].w = w;
            args[n].h = h;
            args[n].tx = tx;
            args[n].ty = ty;
            args[n].color = color;
            job_submit(tile_clear_job, &args[n]);
            n++;
        }
    }
    job_wait_idle();
}

static int imin3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int imax3(int a, int b, int c) {
    int m = a > b ? a : b;
    return m > c ? m : c;
}

void tile_mesh_raster(uint32_t *pixels, int w, int h,
                      const int *sx, const int *sy, const uint32_t *sz,
                      const int32_t *tri_v0, const int32_t *tri_v1,
                      const int32_t *tri_v2, int triangles, int color) {
    TriBin bin;
    static TileTriArg args[TRI_BIN_MAX];
    int i;
    int n = 0;
    int txs = (w + TILE_SIZE - 1) / TILE_SIZE;
    int tys = (h + TILE_SIZE - 1) / TILE_SIZE;

    tri_bin_reset(&bin, w, h);
    for (i = 0; i < triangles; ++i) {
        int i0 = tri_v0[i];
        int i1 = tri_v1[i];
        int i2 = tri_v2[i];
        int minx = imin3(sx[i0], sx[i1], sx[i2]);
        int miny = imin3(sy[i0], sy[i1], sy[i2]);
        int maxx = imax3(sx[i0], sx[i1], sx[i2]);
        int maxy = imax3(sy[i0], sy[i1], sy[i2]);
        tri_bin_add(&bin, minx, miny, maxx, maxy, (uint16_t)i);
    }

    for (i = 0; i < bin.count; ++i) {
        uint32_t e = bin.entries[i];
        uint16_t tri_id = (uint16_t)(e >> 16);
        int ty = (int)((e >> 8) & 0xffu);
        int tx = (int)(e & 0xffu);
        int i0;
        int i1;
        int i2;
        int x0;
        int y0;
        int x1;
        int y1;

        if (tri_id >= (uint16_t)triangles || n >= TRI_BIN_MAX)
            continue;
        if (tx < 0 || ty < 0 || tx >= txs || ty >= tys)
            continue;

        i0 = tri_v0[tri_id];
        i1 = tri_v1[tri_id];
        i2 = tri_v2[tri_id];
        x0 = tx * TILE_SIZE;
        y0 = ty * TILE_SIZE;
        x1 = x0 + TILE_SIZE;
        y1 = y0 + TILE_SIZE;
        if (x1 > w)
            x1 = w;
        if (y1 > h)
            y1 = h;

        args[n].pixels = pixels;
        args[n].w = w;
        args[n].h = h;
        args[n].clip_x0 = x0;
        args[n].clip_y0 = y0;
        args[n].clip_x1 = x1;
        args[n].clip_y1 = y1;
        args[n].x0 = sx[i0];
        args[n].y0 = sy[i0];
        args[n].z0 = (int32_t)sz[i0];
        args[n].x1 = sx[i1];
        args[n].y1 = sy[i1];
        args[n].z1 = (int32_t)sz[i1];
        args[n].x2 = sx[i2];
        args[n].y2 = sy[i2];
        args[n].z2 = (int32_t)sz[i2];
        args[n].color = color;
        job_submit(tile_tri_job, &args[n]);
        n++;
    }
    job_wait_idle();
}
