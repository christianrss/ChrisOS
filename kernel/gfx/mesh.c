#include "mesh.h"

#include "gfx2d.h"
#include "math3d.h"
#include "shade.h"
#include "tile.h"
#include "tri.h"
#include "zbuf.h"

#define MESH_MAX_V 2048
#define MESH_MAX_T 4096

static int g_sx[MESH_MAX_V];
static int g_sy[MESH_MAX_V];
static uint32_t g_sz[MESH_MAX_V];
static int g_vis[MESH_MAX_V];
static float g_wx[MESH_MAX_V];
static float g_wy[MESH_MAX_V];
static float g_wz[MESH_MAX_V];
static int32_t g_t0[MESH_MAX_T];
static int32_t g_t1[MESH_MAX_T];
static int32_t g_t2[MESH_MAX_T];

static int32_t rd_i32(const uint8_t *mem, uint32_t off) {
    uint32_t u = (uint32_t)mem[off] |
                 ((uint32_t)mem[off + 1] << 8) |
                 ((uint32_t)mem[off + 2] << 16) |
                 ((uint32_t)mem[off + 3] << 24);
    return (int32_t)u;
}

static float rd_f32(const uint8_t *mem, uint32_t off) {
    union {
        uint32_t u;
        float f;
    } v;
    v.u = (uint32_t)rd_i32(mem, off);
    return v.f;
}

static float model_to_f(int32_t v) {
    return (float)v / MODEL_SCALE;
}

static void wr_i32(uint8_t *mem, uint32_t off, int32_t v) {
    uint32_t u = (uint32_t)v;
    mem[off] = (uint8_t)(u & 0xffu);
    mem[off + 1] = (uint8_t)((u >> 8) & 0xffu);
    mem[off + 2] = (uint8_t)((u >> 16) & 0xffu);
    mem[off + 3] = (uint8_t)((u >> 24) & 0xffu);
}

static int mesh_ok(ClvmVm *vm, uint32_t *pixels, int32_t addr,
                   int32_t vertices, int32_t triangles, uint32_t *base_out) {
    uint32_t need;
    if (vm == 0 || pixels == 0)
        return 0;
    if (vertices < 3 || triangles < 1)
        return 0;
    if (vertices > MESH_MAX_V || triangles > MESH_MAX_T)
        return 0;
    if (addr < 0)
        return 0;
    *base_out = (uint32_t)addr;
    need = (uint32_t)vertices * 12u + (uint32_t)triangles * 12u;
    if (*base_out >= vm->mem_size || need > vm->mem_size - *base_out)
        return 0;
    return 1;
}

int mesh_draw(ClvmVm *vm, int32_t addr, int32_t vertices, int32_t triangles,
              int32_t angle_deg, int32_t color, uint32_t *pixels, int w, int h) {
    uint32_t base;
    int i;
    int n = 0;
    Mat4f view;
    Mat4f rot;
    Mat4f mvp;

    if (!mesh_ok(vm, pixels, addr, vertices, triangles, &base))
        return -1;
    math3d_set_screen(w, h);
    zbuf_set_size(w, h);
    math3d_view(&view);
    mat4f_rotate_y(&rot, (float)angle_deg);
    mat4f_mul(&mvp, &view, &rot);

    for (i = 0; i < vertices; ++i) {
        uint32_t o = base + (uint32_t)i * 12u;
        float vx = model_to_f(rd_i32(vm->memory, o));
        float vy = model_to_f(rd_i32(vm->memory, o + 4u));
        float vz = model_to_f(rd_i32(vm->memory, o + 8u));
        g_vis[i] = project_vertex(&mvp, vx, vy, vz, &g_sx[i], &g_sy[i], &g_sz[i]);
    }
    for (i = 0; i < triangles; ++i) {
        uint32_t o = base + (uint32_t)vertices * 12u + (uint32_t)i * 12u;
        int32_t i0 = rd_i32(vm->memory, o);
        int32_t i1 = rd_i32(vm->memory, o + 4);
        int32_t i2 = rd_i32(vm->memory, o + 8);
        if (i0 < 0 || i1 < 0 || i2 < 0)
            return -1;
        if (i0 >= vertices || i1 >= vertices || i2 >= vertices)
            return -1;
        if (!g_vis[i0] || !g_vis[i1] || !g_vis[i2])
            continue;
        g_t0[n] = i0;
        g_t1[n] = i1;
        g_t2[n] = i2;
        n++;
    }
    if (n == 0)
        return 0;
#ifdef __freestanding__
    if (w * h >= 640 * 400)
        tile_mesh_raster(pixels, w, h, g_sx, g_sy, g_sz, g_t0, g_t1, g_t2, n, color);
    else
#endif
    {
        for (i = 0; i < n; ++i)
            tri_fill(pixels, w, h,
                     g_sx[g_t0[i]], g_sy[g_t0[i]], (int32_t)g_sz[g_t0[i]],
                     g_sx[g_t1[i]], g_sy[g_t1[i]], (int32_t)g_sz[g_t1[i]],
                     g_sx[g_t2[i]], g_sy[g_t2[i]], (int32_t)g_sz[g_t2[i]],
                     color);
    }
    return 0;
}

int mesh_draw_f(ClvmVm *vm, int32_t addr, int32_t vertices, int32_t triangles,
                float ox, float oy, float oz, float yaw, int32_t color,
                uint32_t *pixels, int w, int h) {
    uint32_t base;
    int i;
    int n = 0;
    Mat4f view;
    Mat4f rot;
    Mat4f trans;
    Mat4f model;
    Mat4f mvp;
    int texid;

    if (!mesh_ok(vm, pixels, addr, vertices, triangles, &base))
        return -1;
    math3d_set_screen(w, h);
    zbuf_set_size(w, h);
    math3d_view(&view);
    mat4f_rotate_y(&rot, yaw);
    mat4f_translate(&trans, ox, oy, oz);
    mat4f_mul(&model, &trans, &rot);
    mat4f_mul(&mvp, &view, &model);
    texid = -1;

    for (i = 0; i < vertices; ++i) {
        uint32_t o = base + (uint32_t)i * 12u;
        Vec3f in;
        Vec3f out;
        in.x = rd_f32(vm->memory, o);
        in.y = rd_f32(vm->memory, o + 4u);
        in.z = rd_f32(vm->memory, o + 8u);
        mat4f_transform(&model, &in, &out);
        g_wx[i] = out.x;
        g_wy[i] = out.y;
        g_wz[i] = out.z;
        g_vis[i] = project_vertex(&mvp, in.x, in.y, in.z, &g_sx[i], &g_sy[i], &g_sz[i]);
    }
    for (i = 0; i < triangles; ++i) {
        uint32_t o = base + (uint32_t)vertices * 12u + (uint32_t)i * 12u;
        int32_t i0 = rd_i32(vm->memory, o);
        int32_t i1 = rd_i32(vm->memory, o + 4);
        int32_t i2 = rd_i32(vm->memory, o + 8);
        if (i0 < 0 || i1 < 0 || i2 < 0)
            return -1;
        if (i0 >= vertices || i1 >= vertices || i2 >= vertices)
            return -1;
        if (!g_vis[i0] || !g_vis[i1] || !g_vis[i2])
            continue;
        g_t0[n] = i0;
        g_t1[n] = i1;
        g_t2[n] = i2;
        n++;
    }
    if (color >= 16)
        texid = (int)(color - 16);
    for (i = 0; i < n; ++i) {
        int a = g_t0[i];
        int b = g_t1[i];
        int c = g_t2[i];
        Vec3f e0;
        Vec3f e1;
        Vec3f nn;
        vec3f_set(&e0, g_wx[b] - g_wx[a], g_wy[b] - g_wy[a], g_wz[b] - g_wz[a]);
        vec3f_set(&e1, g_wx[c] - g_wx[a], g_wy[c] - g_wy[a], g_wz[c] - g_wz[a]);
        vec3f_cross(&nn, &e0, &e1);
        vec3f_norm(&nn);
        if (texid < 0) {
            uint32_t rgb = gfx2d_color(color < 16 ? color : 7);
            rgb = shade_phong(rgb, nn.x, nn.y, nn.z);
            tri_fill_u32(pixels, w, h,
                         g_sx[a], g_sy[a], (int32_t)g_sz[a],
                         g_sx[b], g_sy[b], (int32_t)g_sz[b],
                         g_sx[c], g_sy[c], (int32_t)g_sz[c],
                         rgb, 0, 0, w, h);
        } else {
            tri_fill_lit(pixels, w, h,
                         g_sx[a], g_sy[a], (int32_t)g_sz[a], 0.0f, 0.0f, nn.x, nn.y, nn.z,
                         g_sx[b], g_sy[b], (int32_t)g_sz[b], 1.0f, 0.0f, nn.x, nn.y, nn.z,
                         g_sx[c], g_sy[c], (int32_t)g_sz[c], 0.0f, 1.0f, nn.x, nn.y, nn.z,
                         color < 16 ? color : 7, texid, 0, 0, w, h);
        }
    }
    return 0;
}

int mesh_transform(ClvmVm *vm, int32_t addr, int32_t mat_addr, int32_t n_verts) {
    Mat4f m;
    int i;

    if (vm == 0 || addr < 0 || mat_addr < 0 || n_verts < 1 || n_verts > MESH_MAX_V)
        return -1;
    if ((uint32_t)mat_addr + 64u > vm->mem_size)
        return -1;
    if ((uint32_t)addr + (uint32_t)n_verts * 12u > vm->mem_size)
        return -1;
    for (i = 0; i < 16; ++i) {
        union {
            uint32_t u;
            float f;
        } bits;
        bits.u = (uint32_t)rd_i32(vm->memory, (uint32_t)mat_addr + (uint32_t)i * 4u);
        m.m[i] = bits.f;
    }
    for (i = 0; i < n_verts; ++i) {
        Vec3f in;
        Vec3f out;
        uint32_t o = (uint32_t)addr + (uint32_t)i * 12u;
        in.x = model_to_f(rd_i32(vm->memory, o));
        in.y = model_to_f(rd_i32(vm->memory, o + 4u));
        in.z = model_to_f(rd_i32(vm->memory, o + 8u));
        mat4f_transform(&m, &in, &out);
        wr_i32(vm->memory, o, (int32_t)(out.x * MODEL_SCALE));
        wr_i32(vm->memory, o + 4u, (int32_t)(out.y * MODEL_SCALE));
        wr_i32(vm->memory, o + 8u, (int32_t)(out.z * MODEL_SCALE));
    }
    return 0;
}
