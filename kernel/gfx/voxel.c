#include "voxel.h"

#ifdef __freestanding__
#include "heap.h"
#else
#include <stdlib.h>
static void *kmalloc(uint64_t n) {
    return malloc((size_t)n);
}
#endif
#include "math3d.h"
#include "tex.h"
#include "tri.h"
#include "zbuf.h"

#define CHUNK_N 16
#define WORLD_CX 8
#define WORLD_CY 4
#define WORLD_CZ 8
#define WORLD_SX (WORLD_CX * CHUNK_N)
#define WORLD_SY (WORLD_CY * CHUNK_N)
#define WORLD_SZ (WORLD_CZ * CHUNK_N)
#define CHUNK_VOL (CHUNK_N * CHUNK_N * CHUNK_N)

static uint8_t *g_blocks;
static uint8_t g_dirty[WORLD_CX * WORLD_CY * WORLD_CZ];
static int g_ready;

static const int FACE[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 },
    { 0, 1, 0 }, { 0, -1, 0 },
    { 0, 0, 1 }, { 0, 0, -1 }
};

static int world_index(int x, int y, int z) {
    return (y * WORLD_SZ + z) * WORLD_SX + x;
}

static int in_world(int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < WORLD_SX && y < WORLD_SY && z < WORLD_SZ;
}

static int voxel_init(void) {
    int n;
    if (g_ready)
        return 1;
    n = WORLD_SX * WORLD_SY * WORLD_SZ;
    g_blocks = (uint8_t *)kmalloc((uint64_t)n);
    if (g_blocks == 0)
        return 0;
    {
        int i;
        for (i = 0; i < n; ++i)
            g_blocks[i] = 0;
    }
    {
        int i;
        for (i = 0; i < WORLD_CX * WORLD_CY * WORLD_CZ; ++i)
            g_dirty[i] = 1;
    }
    tex_init();
    g_ready = 1;
    return 1;
}

int voxel_set(int x, int y, int z, int id) {
    int cx;
    int cy;
    int cz;
    if (!voxel_init())
        return -1;
    if (!in_world(x, y, z))
        return -1;
    if (id < 0)
        id = 0;
    if (id > 15)
        id = 15;
    g_blocks[world_index(x, y, z)] = (uint8_t)id;
    cx = x / CHUNK_N;
    cy = y / CHUNK_N;
    cz = z / CHUNK_N;
    g_dirty[cy * WORLD_CX * WORLD_CZ + cz * WORLD_CX + cx] = 1;
    return 0;
}

int voxel_get(int x, int y, int z) {
    if (!voxel_init())
        return 0;
    if (!in_world(x, y, z))
        return 0;
    return (int)g_blocks[world_index(x, y, z)];
}

static int empty_at(int x, int y, int z) {
    if (!in_world(x, y, z))
        return 1;
    return g_blocks[world_index(x, y, z)] == 0;
}

static void emit_face(uint32_t *pixels, int w, int h,
                      int x, int y, int z, int face, int id, const Mat4f *view) {
    float px[4];
    float py[4];
    float pz[4];
    float nx = (float)FACE[face][0];
    float ny = (float)FACE[face][1];
    float nz = (float)FACE[face][2];
    float fx = (float)x;
    float fy = (float)y;
    float fz = (float)z;
    int sx[4];
    int sy[4];
    uint32_t sz[4];
    int vis[4];
    int i;
    float u[4];
    float v[4];
    Vec3f wn;
    Vec3f vn;

    if (face == 0) {
        px[0] = fx + 1; py[0] = fy;     pz[0] = fz;
        px[1] = fx + 1; py[1] = fy;     pz[1] = fz + 1;
        px[2] = fx + 1; py[2] = fy + 1; pz[2] = fz + 1;
        px[3] = fx + 1; py[3] = fy + 1; pz[3] = fz;
    } else if (face == 1) {
        px[0] = fx; py[0] = fy;     pz[0] = fz + 1;
        px[1] = fx; py[1] = fy;     pz[1] = fz;
        px[2] = fx; py[2] = fy + 1; pz[2] = fz;
        px[3] = fx; py[3] = fy + 1; pz[3] = fz + 1;
    } else if (face == 2) {
        px[0] = fx;     py[0] = fy + 1; pz[0] = fz;
        px[1] = fx + 1; py[1] = fy + 1; pz[1] = fz;
        px[2] = fx + 1; py[2] = fy + 1; pz[2] = fz + 1;
        px[3] = fx;     py[3] = fy + 1; pz[3] = fz + 1;
    } else if (face == 3) {
        px[0] = fx;     py[0] = fy; pz[0] = fz + 1;
        px[1] = fx + 1; py[1] = fy; pz[1] = fz + 1;
        px[2] = fx + 1; py[2] = fy; pz[2] = fz;
        px[3] = fx;     py[3] = fy; pz[3] = fz;
    } else if (face == 4) {
        px[0] = fx + 1; py[0] = fy;     pz[0] = fz + 1;
        px[1] = fx;     py[1] = fy;     pz[1] = fz + 1;
        px[2] = fx;     py[2] = fy + 1; pz[2] = fz + 1;
        px[3] = fx + 1; py[3] = fy + 1; pz[3] = fz + 1;
    } else {
        px[0] = fx;     py[0] = fy;     pz[0] = fz;
        px[1] = fx + 1; py[1] = fy;     pz[1] = fz;
        px[2] = fx + 1; py[2] = fy + 1; pz[2] = fz;
        px[3] = fx;     py[3] = fy + 1; pz[3] = fz;
    }
    u[0] = 0.0f; v[0] = 1.0f;
    u[1] = 1.0f; v[1] = 1.0f;
    u[2] = 1.0f; v[2] = 0.0f;
    u[3] = 0.0f; v[3] = 0.0f;
    vec3f_set(&wn, nx, ny, nz);
    mat4f_transform_dir(view, &wn, &vn);
    for (i = 0; i < 4; ++i)
        vis[i] = project_vertex(view, px[i], py[i], pz[i], &sx[i], &sy[i], &sz[i]);
    if (vis[0] && vis[1] && vis[2])
        tri_fill_lit(pixels, w, h,
                     sx[0], sy[0], (int32_t)sz[0], u[0], v[0], vn.x, vn.y, vn.z,
                     sx[1], sy[1], (int32_t)sz[1], u[1], v[1], vn.x, vn.y, vn.z,
                     sx[2], sy[2], (int32_t)sz[2], u[2], v[2], vn.x, vn.y, vn.z,
                     -1, id, 0, 0, w, h);
    if (vis[0] && vis[2] && vis[3])
        tri_fill_lit(pixels, w, h,
                     sx[0], sy[0], (int32_t)sz[0], u[0], v[0], vn.x, vn.y, vn.z,
                     sx[2], sy[2], (int32_t)sz[2], u[2], v[2], vn.x, vn.y, vn.z,
                     sx[3], sy[3], (int32_t)sz[3], u[3], v[3], vn.x, vn.y, vn.z,
                     -1, id, 0, 0, w, h);
}

int voxel_world_draw(uint32_t *pixels, int w, int h) {
    Mat4f view;
    Vec3f cam;
    int x0;
    int y0;
    int z0;
    int x1;
    int y1;
    int z1;
    int x;
    int y;
    int z;
    int face;

    if (pixels == 0 || !voxel_init())
        return -1;
    math3d_set_screen(w, h);
    zbuf_set_size(w, h);
    math3d_view(&view);
    math3d_cam_get(&cam, 0, 0);
    x0 = (int)cam.x - 48;
    y0 = (int)cam.y - 32;
    z0 = (int)cam.z - 48;
    x1 = (int)cam.x + 48;
    y1 = (int)cam.y + 32;
    z1 = (int)cam.z + 48;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 > WORLD_SX)
        x1 = WORLD_SX;
    if (y1 > WORLD_SY)
        y1 = WORLD_SY;
    if (z1 > WORLD_SZ)
        z1 = WORLD_SZ;
    for (y = y0; y < y1; ++y) {
        for (z = z0; z < z1; ++z) {
            for (x = x0; x < x1; ++x) {
                int id = (int)g_blocks[world_index(x, y, z)];
                if (id == 0)
                    continue;
                for (face = 0; face < 6; ++face) {
                    if (!empty_at(x + FACE[face][0], y + FACE[face][1],
                                  z + FACE[face][2]))
                        continue;
                    emit_face(pixels, w, h, x, y, z, face, id, &view);
                }
            }
        }
    }
    return 0;
}
