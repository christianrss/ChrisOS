#ifndef CHRIS_GFX3D_CTX_H
#define CHRIS_GFX3D_CTX_H

#include "math3d.h"
#include "shade.h"
#include "tex.h"

/* Per-app snapshot of the process-wide 3D globals. CLVM loads this before a
 * syscall and saves it on the way out, so two apps do not keep each other's
 * camera, light, or texture binding. The first load installs the default
 * camera, light, and texture; it does not copy whatever the previous app left
 * in the globals. Screen size is taken from the current viewport. The voxel
 * world itself stays singular; voxel_claim rejects a second owner. */

typedef struct Gfx3DCtx {
    int valid;
    Gfx3DView view;
    ShadeState shade;
    TexState tex;
} Gfx3DCtx;

void gfx3d_ctx_load(Gfx3DCtx *ctx);
void gfx3d_ctx_save(Gfx3DCtx *ctx);

#endif
