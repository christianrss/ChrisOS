#ifndef CHRIS_TRI_H
#define CHRIS_TRI_H

#include <stdint.h>

void tri_fill(uint32_t *pixels, int w, int h,
              int x0, int y0, int32_t z0,
              int x1, int y1, int32_t z1,
              int x2, int y2, int32_t z2,
              int color);

void tri_fill_clip(uint32_t *pixels, int w, int h,
                   int x0, int y0, int32_t z0,
                   int x1, int y1, int32_t z1,
                   int x2, int y2, int32_t z2,
                   int color,
                   int clip_x0, int clip_y0, int clip_x1, int clip_y1);

void tri_fill_u32(uint32_t *pixels, int w, int h,
                  int x0, int y0, int32_t z0,
                  int x1, int y1, int32_t z1,
                  int x2, int y2, int32_t z2,
                  uint32_t rgb,
                  int clip_x0, int clip_y0, int clip_x1, int clip_y1);

void tri_fill_lit(uint32_t *pixels, int w, int h,
                  int x0, int y0, int32_t z0, float u0, float v0,
                  float nx0, float ny0, float nz0,
                  int x1, int y1, int32_t z1, float u1, float v1,
                  float nx1, float ny1, float nz1,
                  int x2, int y2, int32_t z2, float u2, float v2,
                  float nx2, float ny2, float nz2,
                  int color, int texid,
                  int clip_x0, int clip_y0, int clip_x1, int clip_y1);

#endif
