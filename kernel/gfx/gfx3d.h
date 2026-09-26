#ifndef CHRIS_GFX3D_H
#define CHRIS_GFX3D_H

#include <stdint.h>

#include "math3d.h"
#include "shader/sh_pub.h"

/* Backend-neutral 3D API. VirGL and VirtIO ids never cross this boundary.
 * Public handles are index + generation. 0 is never valid. */

#define GFX3D_AUTO 0
#define GFX3D_SOFTWARE 1
#define GFX3D_VIRGL 2
#define GFX3D_MOCK 3

#define GFX3D_USAGE_STATIC 0
#define GFX3D_USAGE_DYNAMIC 1
#define GFX3D_USAGE_STREAM 2

#define GFX3D_OWNER_KERNEL 0

typedef struct Gfx3DLayout {
    int stride;
    int nelem;
    int location[8];
    int components[8];
    int offset[8];
} Gfx3DLayout;

typedef struct Gfx3DStats {
    uint32_t submits;
    uint32_t dwords;
    uint32_t draws;
    uint32_t triangles;
    uint32_t uploads;
    uint32_t bytes_up;
    uint32_t readbacks;
    uint32_t frame_cycles;
    uint32_t cpu_mesh_bytes;
    uint32_t gpu_backing_bytes;
    uint32_t target_bytes;
    uint32_t depth_bytes;
    uint32_t staging_bytes;
    int live_mesh;
    int live_tex;
    int live_target;
    int live_prog;
    int live_ctx;
    int virgl_obj_live;
    int virgl_obj_peak;
    int visible_chunks;
    int chunk_rebuilds;
    int backend;
    int forced;
    int degraded;
} Gfx3DStats;

int gfx3d_boot(int mode);
int gfx3d_backend(void);
const char *gfx3d_backend_name(void);
int gfx3d_forced(void);
void gfx3d_mark_lost(void);

uint32_t gfx3d_context_create(int owner);
int gfx3d_context_destroy(int owner, uint32_t ctx);

uint32_t gfx3d_target_create(int owner, uint32_t ctx, int w, int h);
int gfx3d_target_resize(int owner, uint32_t target, int w, int h);
int gfx3d_target_destroy(int owner, uint32_t target);
int gfx3d_target_read(int owner, uint32_t target, uint32_t *dst, int npixels);

uint32_t gfx3d_mesh_create(int owner, int usage);
int gfx3d_mesh_upload(int owner, uint32_t mesh, const Gfx3DLayout *layout, const void *verts,
                      int vbytes, const uint16_t *idx, int nidx);
int gfx3d_mesh_vert(int owner, uint32_t mesh, float x, float y, float z, float nx, float ny,
                    float nz, float u, float v);
int gfx3d_mesh_finish(int owner, uint32_t mesh);
int gfx3d_mesh_destroy(int owner, uint32_t mesh);

uint32_t gfx3d_tex_create(int owner, int w, int h);
int gfx3d_tex_upload(int owner, uint32_t tex, const uint32_t *pixels, int w, int h);
int gfx3d_tex_solid(int owner, uint32_t *out, uint32_t argb);
int gfx3d_tex_destroy(int owner, uint32_t tex);

uint32_t gfx3d_prog_prepare(int owner, uint32_t ctx, ShProgram *prog);
int gfx3d_prog_destroy(int owner, uint32_t inst);

int gfx3d_begin(int owner, uint32_t ctx, uint32_t target);
int gfx3d_clear(int owner, uint32_t ctx, float r, float g, float b, float a, int depth);
int gfx3d_viewport(int owner, uint32_t ctx, int x, int y, int w, int h);
int gfx3d_depth(int owner, uint32_t ctx, int enable);
int gfx3d_cull(int owner, uint32_t ctx, int enable);
int gfx3d_camera(int owner, uint32_t ctx, float x, float y, float z, float yaw, float pitch,
                 float fov_deg, float znear, float zfar);
int gfx3d_model(int owner, uint32_t ctx, const Mat4f *model);
int gfx3d_use(int owner, uint32_t ctx, uint32_t inst);
int gfx3d_uniform_mat4(int owner, uint32_t ctx, int loc, const Mat4f *m);
int gfx3d_uniform_f(int owner, uint32_t ctx, int loc, const float *v, int n);
int gfx3d_bind_tex(int owner, uint32_t ctx, int unit, uint32_t tex);
int gfx3d_draw(int owner, uint32_t ctx, uint32_t mesh);
int gfx3d_end(int owner, uint32_t ctx);
int gfx3d_present_scanout(int owner, uint32_t target);
int gfx3d_scanout_primary(void);

void gfx3d_drop_owner(int owner);
void gfx3d_stats(Gfx3DStats *out);
void gfx3d_stats_reset(void);
int gfx3d_layout_ok(const ShProgram *prog, const Gfx3DLayout *layout);

/* Returns 0 when this frame was presented into pixels. 1 means the caller
 * should draw with the software scene. -1 is a forced-backend failure. */
int gfx3d_present_window(int owner, uint32_t *pixels, int w, int h);
int gfx3d_world_draw(int owner, uint32_t *pixels, int w, int h);
int gfx3d_meshf_draw(int owner, const float *pos, int vertices, const int32_t *idx, int triangles,
                     float ox, float oy, float oz, float yaw, int color, uint32_t *pixels, int w,
                     int h);

int gfx3d_mock_log(char *dst, int cap);
const char *gfx3d_last_error(void);

#endif
