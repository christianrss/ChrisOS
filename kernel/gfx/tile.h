#ifndef CHRIS_TILE_H
#define CHRIS_TILE_H

#include <stdint.h>

#define TILE_SIZE 64

void tile_parallel_clear(uint32_t *dest, int w, int h, uint32_t color);

void tile_mesh_raster(uint32_t *pixels, int w, int h,
                      const int *sx, const int *sy, const uint32_t *sz,
                      const int32_t *tri_v0, const int32_t *tri_v1,
                      const int32_t *tri_v2, int triangles, int color);

#endif
