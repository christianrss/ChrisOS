#ifndef CHRIS_GFX3D_DEV_H
#define CHRIS_GFX3D_DEV_H

#include <stdint.h>

#include "gfx3d.h"

/* Device half of the VirGL backend. Host builds supply stubs. */

typedef struct Gfx3DDevDraw {
    uint32_t vs;
    uint32_t fs;
    ShProgram *prog;
    uint32_t vbo;
    uint32_t ib;
    uint32_t stride;
    uint32_t count;
    int indexed;
    int depth;
    int cull;
    int textured;
    uint32_t tex;
    uint32_t view;
    int nelem;
    uint32_t off[8];
    uint32_t fmt[8];
    uint32_t color_res;
    uint32_t depth_res;
    int width;
    int height;
} Gfx3DDevDraw;

int gfx3d_dev_available(void);
int gfx3d_dev_ctx(int owner, uint32_t *ctx);
int gfx3d_dev_ctx_destroy(int owner, uint32_t ctx);
int gfx3d_dev_target(int owner, uint32_t ctx, int w, int h, uint32_t *color, uint32_t *depth,
                     int *dma);
int gfx3d_dev_target_destroy(int owner, uint32_t ctx, uint32_t color, uint32_t depth, int dma);
int gfx3d_dev_read(uint32_t ctx, uint32_t color, int w, int h, uint32_t *dst);
int gfx3d_dev_buffer(int owner, uint32_t ctx, uint32_t bind, const void *src, uint32_t bytes,
                     uint32_t *id);
int gfx3d_dev_buffer_destroy(int owner, uint32_t ctx, uint32_t id);
int gfx3d_dev_tex(int owner, uint32_t ctx, int w, int h, const uint32_t *bgra, uint32_t *id,
                  uint32_t *view);
int gfx3d_dev_tex_destroy(int owner, uint32_t ctx, uint32_t id, uint32_t view);
int gfx3d_dev_shader(uint32_t ctx, int stage, const char *tgsi, uint32_t *handle);
int gfx3d_dev_shader_destroy(uint32_t ctx, uint32_t handle);
int gfx3d_dev_frame_begin(uint32_t ctx);
int gfx3d_dev_clear(uint32_t ctx, const Gfx3DDevDraw *d, float r, float g, float b, float a,
                    int depth);
int gfx3d_dev_draw(uint32_t ctx, const Gfx3DDevDraw *d);
int gfx3d_dev_frame_end(uint32_t ctx);
int gfx3d_dev_scanout(uint32_t color, int w, int h);
void gfx3d_dev_obj_counts(int *live, int *peak);
uint32_t gfx3d_dev_submits(void);
uint32_t gfx3d_dev_dwords(void);
uint32_t gfx3d_dev_backing(void);

#endif
