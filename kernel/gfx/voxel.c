#include "voxel.h"

#ifdef __freestanding__
#include "heap.h"
#else
#include <stdlib.h>
static void *kmalloc(uint64_t n) {
    return malloc((size_t)n);
}
static void kfree(void *p) {
    free(p);
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
#define CHUNK_COUNT (WORLD_CX * WORLD_CY * WORLD_CZ)
#define FACE_CAP_MAX 8192

typedef struct {
    uint8_t lx;
    uint8_t ly;
    uint8_t lz;
    uint8_t face;
    uint8_t id;
} MeshFace;

typedef struct {
    MeshFace *faces;
    int n;
    int cap;
} ChunkMesh;

static uint8_t *g_blocks;
static uint8_t g_dirty[CHUNK_COUNT];
static ChunkMesh g_mesh[CHUNK_COUNT];
static int g_ready;
static int g_rebuilds;

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

static int chunk_index(int cx, int cy, int cz) {
    return cy * WORLD_CX * WORLD_CZ + cz * WORLD_CX + cx;
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
        for (i = 0; i < CHUNK_COUNT; ++i) {
            g_dirty[i] = 1;
            g_mesh[i].faces = 0;
            g_mesh[i].n = 0;
            g_mesh[i].cap = 0;
        }
    }
    tex_init();
    g_rebuilds = 0;
    g_ready = 1;
    return 1;
}

static void mark_chunk(int cx, int cy, int cz) {
    if (cx < 0 || cy < 0 || cz < 0 || cx >= WORLD_CX || cy >= WORLD_CY ||
        cz >= WORLD_CZ)
        return;
    g_dirty[chunk_index(cx, cy, cz)] = 1;
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
    mark_chunk(cx, cy, cz);
    if (x % CHUNK_N == 0)
        mark_chunk(cx - 1, cy, cz);
    if (x % CHUNK_N == CHUNK_N - 1)
        mark_chunk(cx + 1, cy, cz);
    if (y % CHUNK_N == 0)
        mark_chunk(cx, cy - 1, cz);
    if (y % CHUNK_N == CHUNK_N - 1)
        mark_chunk(cx, cy + 1, cz);
    if (z % CHUNK_N == 0)
        mark_chunk(cx, cy, cz - 1);
    if (z % CHUNK_N == CHUNK_N - 1)
        mark_chunk(cx, cy, cz + 1);
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

static int mesh_push(ChunkMesh *m, uint8_t lx, uint8_t ly, uint8_t lz,
                     uint8_t face, uint8_t id) {
    if (m->n == m->cap) {
        int ncap = m->cap == 0 ? 64 : m->cap * 2;
        MeshFace *nf;
        int i;
        if (ncap > FACE_CAP_MAX)
            ncap = FACE_CAP_MAX;
        if (m->n >= ncap)
            return 0;
        nf = (MeshFace *)kmalloc((uint64_t)ncap * sizeof(MeshFace));
        if (nf == 0)
            return 0;
        for (i = 0; i < m->n; ++i)
            nf[i] = m->faces[i];
        if (m->faces)
            kfree(m->faces);
        m->faces = nf;
        m->cap = ncap;
    }
    m->faces[m->n].lx = lx;
    m->faces[m->n].ly = ly;
    m->faces[m->n].lz = lz;
    m->faces[m->n].face = face;
    m->faces[m->n].id = id;
    m->n++;
    return 1;
}

static void rebuild_chunk(int cx, int cy, int cz) {
    int ci = chunk_index(cx, cy, cz);
    ChunkMesh *m = &g_mesh[ci];
    int ox = cx * CHUNK_N;
    int oy = cy * CHUNK_N;
    int oz = cz * CHUNK_N;
    int lx;
    int ly;
    int lz;
    int face;

    m->n = 0;
    for (ly = 0; ly < CHUNK_N; ++ly) {
        for (lz = 0; lz < CHUNK_N; ++lz) {
            for (lx = 0; lx < CHUNK_N; ++lx) {
                int x = ox + lx;
                int y = oy + ly;
                int z = oz + lz;
                int id = (int)g_blocks[world_index(x, y, z)];
                if (id == 0)
                    continue;
                for (face = 0; face < 6; ++face) {
                    if (!empty_at(x + FACE[face][0], y + FACE[face][1],
                                  z + FACE[face][2]))
                        continue;
                    if (!mesh_push(m, (uint8_t)lx, (uint8_t)ly, (uint8_t)lz,
                                   (uint8_t)face, (uint8_t)id))
                        return;
                }
            }
        }
    }
    g_dirty[ci] = 0;
    g_rebuilds++;
}

static void face_verts(int x, int y, int z, int face, float *px, float *py,
                       float *pz) {
    float fx = (float)x;
    float fy = (float)y;
    float fz = (float)z;

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
}

typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
} ClipV;

static int clip_near_tri(const ClipV in[3], ClipV out[4]) {
    const float near = 0.08f;
    int i;
    int n = 0;
    for (i = 0; i < 3; ++i) {
        ClipV a = in[i];
        ClipV b = in[(i + 1) % 3];
        int ain = a.z >= near;
        int bin = b.z >= near;
        if (ain && bin) {
            out[n++] = b;
        } else if (ain != bin && b.z != a.z) {
            float t = (near - a.z) / (b.z - a.z);
            ClipV p;
            p.x = a.x + (b.x - a.x) * t;
            p.y = a.y + (b.y - a.y) * t;
            p.z = near;
            p.u = a.u + (b.u - a.u) * t;
            p.v = a.v + (b.v - a.v) * t;
            out[n++] = p;
            if (bin)
                out[n++] = b;
        }
    }
    return n;
}

static void draw_clipped(uint32_t *pixels, int w, int h, const ClipV *poly, int n,
                         int id, float nx, float ny, float nz) {
    int sx[4];
    int sy[4];
    uint32_t sz[4];
    int i;
    if (n < 3)
        return;
    for (i = 0; i < n; ++i) {
        if (!project_view(poly[i].x, poly[i].y, poly[i].z, &sx[i], &sy[i], &sz[i]))
            return;
    }
    tri_fill_tex(pixels, w, h,
                 sx[0], sy[0], (int32_t)sz[0], poly[0].u, poly[0].v,
                 sx[1], sy[1], (int32_t)sz[1], poly[1].u, poly[1].v,
                 sx[2], sy[2], (int32_t)sz[2], poly[2].u, poly[2].v,
                 id, nx, ny, nz, 0, 0, w, h);
    if (n >= 4)
        tri_fill_tex(pixels, w, h,
                     sx[0], sy[0], (int32_t)sz[0], poly[0].u, poly[0].v,
                     sx[2], sy[2], (int32_t)sz[2], poly[2].u, poly[2].v,
                     sx[3], sy[3], (int32_t)sz[3], poly[3].u, poly[3].v,
                     id, nx, ny, nz, 0, 0, w, h);
}

static void draw_face(uint32_t *pixels, int w, int h, int x, int y, int z,
                      int face, int id, const Mat4f *view) {
    float px[4];
    float py[4];
    float pz[4];
    float nx = (float)FACE[face][0];
    float ny = (float)FACE[face][1];
    float nz = (float)FACE[face][2];
    ClipV cam[4];
    ClipV tri[3];
    ClipV poly[4];
    int i;
    int n;
    Vec3f wn;
    Vec3f vn;
    Vec3f world;
    Vec3f eye;
    static const float tu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    static const float tv[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    static const int fan[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };

    face_verts(x, y, z, face, px, py, pz);
    vec3f_set(&wn, nx, ny, nz);
    mat4f_transform_dir(view, &wn, &vn);
    vec3f_norm(&vn);
    for (i = 0; i < 4; ++i) {
        vec3f_set(&world, px[i], py[i], pz[i]);
        mat4f_transform(view, &world, &eye);
        cam[i].x = eye.x;
        cam[i].y = eye.y;
        cam[i].z = eye.z;
        cam[i].u = tu[i];
        cam[i].v = tv[i];
    }
    for (i = 0; i < 2; ++i) {
        tri[0] = cam[fan[i][0]];
        tri[1] = cam[fan[i][1]];
        tri[2] = cam[fan[i][2]];
        n = clip_near_tri(tri, poly);
        draw_clipped(pixels, w, h, poly, n, id, vn.x, vn.y, vn.z);
    }
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
    int cx;
    int cy;
    int cz;

    if (pixels == 0 || !voxel_init())
        return -1;
    math3d_set_screen(w, h);
    zbuf_set_size(w, h);
    zbuf_clear();
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

    for (cy = 0; cy < WORLD_CY; ++cy) {
        int oy = cy * CHUNK_N;
        if (oy + CHUNK_N <= y0 || oy >= y1)
            continue;
        for (cz = 0; cz < WORLD_CZ; ++cz) {
            int oz = cz * CHUNK_N;
            if (oz + CHUNK_N <= z0 || oz >= z1)
                continue;
            for (cx = 0; cx < WORLD_CX; ++cx) {
                int ox = cx * CHUNK_N;
                int ci;
                ChunkMesh *m;
                int i;
                if (ox + CHUNK_N <= x0 || ox >= x1)
                    continue;
                ci = chunk_index(cx, cy, cz);
                if (g_dirty[ci])
                    rebuild_chunk(cx, cy, cz);
                m = &g_mesh[ci];
                for (i = 0; i < m->n; ++i) {
                    MeshFace *f = &m->faces[i];
                    draw_face(pixels, w, h, ox + f->lx, oy + f->ly, oz + f->lz,
                              f->face, f->id, &view);
                }
            }
        }
    }
    return 0;
}

int voxel_mesh_rebuilds(void) {
    return g_rebuilds;
}
