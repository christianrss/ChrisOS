#ifndef CHRIS_GFX3D_BATCH_H
#define CHRIS_GFX3D_BATCH_H

#include <stdint.h>

#include "virgl_cmd.h"

#define GFX3D_BATCH_DWORDS VIRGL_CMD_MAX

typedef struct Gfx3DBatch {
    uint32_t d[GFX3D_BATCH_DWORDS];
    VirglCmd cmd;
    uint32_t ctx;
    int (*submit)(void *user, uint32_t ctx, const uint32_t *dwords, uint32_t n);
    void *user;
    uint32_t submits;
    uint32_t dwords;
    int err;
} Gfx3DBatch;

int gfx3d_batch_open(Gfx3DBatch *b, uint32_t ctx,
                     int (*submit)(void *, uint32_t, const uint32_t *, uint32_t), void *user);
int gfx3d_batch_reserve(Gfx3DBatch *b, uint32_t need);
int gfx3d_batch_flush(Gfx3DBatch *b);
VirglCmd *gfx3d_batch_cmd(Gfx3DBatch *b);
uint32_t gfx3d_batch_submits(const Gfx3DBatch *b);

#endif
