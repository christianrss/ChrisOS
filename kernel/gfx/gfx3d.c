#include "gfx3d.h"

#include "gfx3d_dev.h"

#include <string.h>

#ifdef __freestanding__
#include "heap.h"
#include "vgpu.h"
#else
#include <stdlib.h>
#endif

#define CTX_N 8
#define TGT_N 8
#define MESH_N 48
#define TEX_N 24
#define PROG_N 16
#define ACC_MAX 8192

typedef struct CtxSlot {
    uint32_t gen;
    int live;
    int owner;
    uint32_t dev;
    int frame;
    uint32_t target;
    uint32_t prog;
    uint32_t tex;
    int depth_on;
    int cull;
    int vp_w;
    int vp_h;
    Mat4f model;
    Mat4f view;
    Mat4f proj;
    int have_cam;
    float fov;
    float znear;
    float zfar;
} CtxSlot;

typedef struct TgtSlot {
    uint32_t gen;
    int live;
    int owner;
    uint32_t ctx;
    int w;
    int h;
    uint32_t *color;
    float *depth;
    uint32_t dev_color;
    uint32_t dev_depth;
    int dma;
} TgtSlot;

typedef struct MeshSlot {
    uint32_t gen;
    int live;
    int owner;
    int usage;
    int uploaded;
    Gfx3DLayout lay;
    uint8_t *verts;
    int vbytes;
    uint16_t *idx;
    int nidx;
    float *acc;
    int nacc;
    int acc_cap;
    uint32_t dev_vbo;
    uint32_t dev_ib;
    uint32_t dev_ctx;
} MeshSlot;

typedef struct TexSlot {
    uint32_t gen;
    int live;
    int owner;
    int w;
    int h;
    uint32_t *pix;
    uint32_t dev;
    uint32_t view;
    uint32_t dev_ctx;
} TexSlot;

typedef struct ProgSlot {
    uint32_t gen;
    int live;
    int owner;
    uint32_t ctx;
    ShProgram *prog;
    uint32_t stamp;
    uint32_t vs;
    uint32_t fs;
    uint32_t dev_ctx;
} ProgSlot;

static CtxSlot g_ctx[CTX_N];
static TgtSlot g_tgt[TGT_N];
static MeshSlot g_mesh[MESH_N];
static TexSlot g_tex[TEX_N];
static ProgSlot g_prog[PROG_N];
static int g_backend = GFX3D_SOFTWARE;
static int g_force = GFX3D_AUTO;
static int g_lost = 0;
static Gfx3DStats g_stats;
static char g_err[96];
static char g_log[1024];
static int g_logn;
static int g_implicit;
static uint32_t g_seen_sub;
static uint32_t g_seen_dw;
static int g_imp_owner = -1;
static uint32_t g_imp_ctx;
static uint32_t g_imp_tgt;

static void *gmem(uint64_t n) {
#ifdef __freestanding__
    return kmalloc(n);
#else
    return malloc((size_t)n);
#endif
}

static void grel(void *p) {
#ifdef __freestanding__
    kfree(p);
#else
    free(p);
#endif
}

static void set_err(const char *s) {
    int i = 0;
    if (!s) {
        g_err[0] = 0;
        return;
    }
    while (s[i] && i < (int)sizeof(g_err) - 1) {
        g_err[i] = s[i];
        ++i;
    }
    g_err[i] = 0;
}

const char *gfx3d_last_error(void) {
    return g_err;
}

static void log_add(const char *s) {
    int i = 0;
    while (s[i] && g_logn < (int)sizeof(g_log) - 1) {
        g_log[g_logn++] = s[i++];
    }
    g_log[g_logn] = 0;
}

int gfx3d_mock_log(char *dst, int cap) {
    int i = 0;
    if (!dst || cap < 1) {
        return -1;
    }
    while (g_log[i] && i + 1 < cap) {
        dst[i] = g_log[i];
        ++i;
    }
    dst[i] = 0;
    return i;
}

static int virgl_on(void) {
    return g_backend == GFX3D_VIRGL && !g_lost;
}

int gfx3d_boot(int mode) {
    g_force = mode;
    g_lost = 0;
    set_err(0);
    if (mode == GFX3D_MOCK) {
        g_backend = GFX3D_MOCK;
        return 0;
    }
    if (mode == GFX3D_SOFTWARE) {
        g_backend = GFX3D_SOFTWARE;
        return 0;
    }
    if (mode == GFX3D_VIRGL) {
        if (gfx3d_dev_available()) {
            g_backend = GFX3D_VIRGL;
            return 0;
        }
        g_lost = 1;
        g_backend = GFX3D_VIRGL;
        set_err("virgl unavailable");
        return -1;
    }
    if (gfx3d_dev_available()) {
        g_backend = GFX3D_VIRGL;
        return 0;
    }
    g_backend = GFX3D_SOFTWARE;
    return 0;
}

void gfx3d_mark_lost(void) {
    g_lost = 1;
    set_err("virgl lost");
    if (g_force != GFX3D_VIRGL) {
        g_backend = GFX3D_SOFTWARE;
        g_stats.degraded = 1;
    }
}

int gfx3d_backend(void) {
    return g_backend;
}

const char *gfx3d_backend_name(void) {
    if (g_backend == GFX3D_VIRGL) {
        return g_lost ? "virgl-lost" : "virgl";
    }
    if (g_backend == GFX3D_MOCK) {
        return "mock";
    }
    return "software";
}

int gfx3d_forced(void) {
    return g_force;
}

static uint32_t pack(int index, uint32_t gen) {
    return (gen << 16) | (uint32_t)(index + 1);
}

static int slot_of(uint32_t h, int n, uint32_t gen, int live) {
    int ix;
    if (h == 0u || gen == 0u || !live) {
        return -1;
    }
    ix = (int)(h & 0xffffu) - 1;
    if (ix < 0 || ix >= n) {
        return -1;
    }
    if ((h >> 16) != gen) {
        return -1;
    }
    return ix;
}

static CtxSlot *ctx_at(int owner, uint32_t h) {
    int i;
    for (i = 0; i < CTX_N; ++i) {
        if (slot_of(h, CTX_N, g_ctx[i].gen, g_ctx[i].live) == i &&
            (owner < 0 || g_ctx[i].owner == owner)) {
            return &g_ctx[i];
        }
    }
    return 0;
}

static TgtSlot *tgt_at(int owner, uint32_t h) {
    int i;
    for (i = 0; i < TGT_N; ++i) {
        if (slot_of(h, TGT_N, g_tgt[i].gen, g_tgt[i].live) == i &&
            (owner < 0 || g_tgt[i].owner == owner)) {
            return &g_tgt[i];
        }
    }
    return 0;
}

static MeshSlot *mesh_at(int owner, uint32_t h) {
    int i;
    for (i = 0; i < MESH_N; ++i) {
        if (slot_of(h, MESH_N, g_mesh[i].gen, g_mesh[i].live) == i &&
            (owner < 0 || g_mesh[i].owner == owner)) {
            return &g_mesh[i];
        }
    }
    return 0;
}

static TexSlot *tex_at(int owner, uint32_t h) {
    int i;
    for (i = 0; i < TEX_N; ++i) {
        if (slot_of(h, TEX_N, g_tex[i].gen, g_tex[i].live) == i &&
            (owner < 0 || g_tex[i].owner == owner)) {
            return &g_tex[i];
        }
    }
    return 0;
}

static ProgSlot *prog_at(int owner, uint32_t h) {
    int i;
    for (i = 0; i < PROG_N; ++i) {
        if (slot_of(h, PROG_N, g_prog[i].gen, g_prog[i].live) == i &&
            (owner < 0 || g_prog[i].owner == owner)) {
            return &g_prog[i];
        }
    }
    return 0;
}

static uint32_t take_gen(uint32_t gen) {
    gen++;
    if (gen == 0u || gen > 0xffffu) {
        gen = 1;
    }
    return gen;
}

uint32_t gfx3d_context_create(int owner) {
    int i;
    for (i = 0; i < CTX_N; ++i) {
        if (!g_ctx[i].live) {
            uint32_t dev = 0;
            uint32_t gen = take_gen(g_ctx[i].gen);
            memset(&g_ctx[i], 0, sizeof(g_ctx[i]));
            g_ctx[i].gen = gen;
            g_ctx[i].live = 1;
            g_ctx[i].owner = owner;
            g_ctx[i].depth_on = 1;
            g_ctx[i].fov = 60.0f;
            g_ctx[i].znear = 0.1f;
            g_ctx[i].zfar = 200.0f;
            mat4f_identity(&g_ctx[i].model);
            mat4f_identity(&g_ctx[i].view);
            mat4f_identity(&g_ctx[i].proj);
            if (virgl_on()) {
                if (gfx3d_dev_ctx(owner, &dev) != 0) {
                    set_err("context create failed");
                    if (g_force == GFX3D_VIRGL) {
                        g_ctx[i].live = 0;
                        return 0;
                    }
                    gfx3d_mark_lost();
                } else {
                    g_ctx[i].dev = dev;
                }
            }
            g_stats.live_ctx++;
            return pack(i, g_ctx[i].gen);
        }
    }
    set_err("context table full");
    return 0;
}

static void mesh_free_buf(MeshSlot *m) {
    if (m->verts) {
        g_stats.cpu_mesh_bytes -= (uint32_t)m->vbytes;
        grel(m->verts);
        m->verts = 0;
    }
    if (m->idx) {
        grel(m->idx);
        m->idx = 0;
    }
    if (m->acc) {
        grel(m->acc);
        m->acc = 0;
    }
    m->vbytes = 0;
    m->nidx = 0;
    m->nacc = 0;
}

static void release_mesh_dev(MeshSlot *m) {
    if (m->dev_vbo) {
        (void)gfx3d_dev_buffer_destroy(m->owner, m->dev_ctx, m->dev_vbo);
        m->dev_vbo = 0;
    }
    if (m->dev_ib) {
        (void)gfx3d_dev_buffer_destroy(m->owner, m->dev_ctx, m->dev_ib);
        m->dev_ib = 0;
    }
}

int gfx3d_context_destroy(int owner, uint32_t ctx) {
    CtxSlot *c = ctx_at(owner, ctx);
    int i;
    if (!c) {
        return -1;
    }
    for (i = 0; i < TGT_N; ++i) {
        if (g_tgt[i].live && g_tgt[i].ctx == ctx) {
            (void)gfx3d_target_destroy(owner, pack(i, g_tgt[i].gen));
        }
    }
    for (i = 0; i < PROG_N; ++i) {
        if (g_prog[i].live && g_prog[i].ctx == ctx) {
            (void)gfx3d_prog_destroy(owner, pack(i, g_prog[i].gen));
        }
    }
    if (c->dev) {
        (void)gfx3d_dev_ctx_destroy(owner, c->dev);
    }
    c->live = 0;
    c->dev = 0;
    c->frame = 0;
    if (g_stats.live_ctx > 0) {
        g_stats.live_ctx--;
    }
    return 0;
}

static int alloc_target_cpu(TgtSlot *t, int w, int h) {
    uint32_t n = (uint32_t)w * (uint32_t)h;
    t->color = (uint32_t *)gmem((uint64_t)n * 4u);
    t->depth = (float *)gmem((uint64_t)n * sizeof(float));
    if (!t->color || !t->depth) {
        grel(t->color);
        grel(t->depth);
        t->color = 0;
        t->depth = 0;
        set_err("target alloc failed");
        return -1;
    }
    t->w = w;
    t->h = h;
    g_stats.target_bytes += n * 4u;
    g_stats.depth_bytes += n * (uint32_t)sizeof(float);
    return 0;
}

static void free_target_cpu(TgtSlot *t) {
    uint32_t n;
    if (!t->w || !t->h) {
        grel(t->color);
        grel(t->depth);
        t->color = 0;
        t->depth = 0;
        return;
    }
    n = (uint32_t)t->w * (uint32_t)t->h;
    if (t->color) {
        g_stats.target_bytes -= n * 4u;
    }
    if (t->depth) {
        g_stats.depth_bytes -= n * (uint32_t)sizeof(float);
    }
    grel(t->color);
    grel(t->depth);
    t->color = 0;
    t->depth = 0;
}

uint32_t gfx3d_target_create(int owner, uint32_t ctx, int w, int h) {
    CtxSlot *c = ctx_at(owner, ctx);
    int i;
    if (!c || w < 1 || h < 1 || w > 1920 || h > 1080) {
        set_err("bad target");
        return 0;
    }
    for (i = 0; i < TGT_N; ++i) {
        if (!g_tgt[i].live) {
            uint32_t gen = take_gen(g_tgt[i].gen);
            memset(&g_tgt[i], 0, sizeof(g_tgt[i]));
            g_tgt[i].gen = gen;
            g_tgt[i].live = 1;
            g_tgt[i].owner = owner;
            g_tgt[i].ctx = ctx;
            g_tgt[i].dma = -1;
            if (alloc_target_cpu(&g_tgt[i], w, h) != 0) {
                g_tgt[i].live = 0;
                return 0;
            }
            if (virgl_on() && c->dev) {
                if (gfx3d_dev_target(owner, c->dev, w, h, &g_tgt[i].dev_color, &g_tgt[i].dev_depth,
                                     &g_tgt[i].dma) != 0) {
                    free_target_cpu(&g_tgt[i]);
                    g_tgt[i].live = 0;
                    set_err("virgl target failed");
                    if (g_force == GFX3D_VIRGL) {
                        return 0;
                    }
                    gfx3d_mark_lost();
                }
            }
            g_stats.live_target++;
            return pack(i, g_tgt[i].gen);
        }
    }
    set_err("target table full");
    return 0;
}

int gfx3d_target_resize(int owner, uint32_t target, int w, int h) {
    TgtSlot *t = tgt_at(owner, target);
    CtxSlot *c;
    if (!t || w < 1 || h < 1 || w > 1920 || h > 1080) {
        return -1;
    }
    if (t->w == w && t->h == h) {
        return 0;
    }
    c = ctx_at(owner, t->ctx);
    if (t->dev_color && c && c->dev) {
        (void)gfx3d_dev_target_destroy(owner, c->dev, t->dev_color, t->dev_depth, t->dma);
        t->dev_color = 0;
        t->dev_depth = 0;
        t->dma = -1;
    }
    free_target_cpu(t);
    if (alloc_target_cpu(t, w, h) != 0) {
        return -1;
    }
    if (virgl_on() && c && c->dev) {
        if (gfx3d_dev_target(owner, c->dev, w, h, &t->dev_color, &t->dev_depth, &t->dma) != 0) {
            set_err("virgl resize failed");
            if (g_force == GFX3D_VIRGL) {
                return -1;
            }
            gfx3d_mark_lost();
        }
    }
    return 0;
}

int gfx3d_target_destroy(int owner, uint32_t target) {
    TgtSlot *t = tgt_at(owner, target);
    CtxSlot *c;
    if (!t) {
        return -1;
    }
    c = ctx_at(owner, t->ctx);
    if (t->dev_color && c && c->dev) {
        (void)gfx3d_dev_target_destroy(owner, c->dev, t->dev_color, t->dev_depth, t->dma);
    }
    free_target_cpu(t);
    t->live = 0;
    t->dev_color = 0;
    t->dev_depth = 0;
    if (g_stats.live_target > 0) {
        g_stats.live_target--;
    }
    return 0;
}

int gfx3d_target_read(int owner, uint32_t target, uint32_t *dst, int npixels) {
    TgtSlot *t = tgt_at(owner, target);
    CtxSlot *c;
    int i;
    int n;
    if (!t || !dst || !t->color) {
        return -1;
    }
    n = t->w * t->h;
    if (npixels < n) {
        return -1;
    }
    c = ctx_at(owner, t->ctx);
    if (virgl_on() && c && c->dev && t->dev_color) {
        if (gfx3d_dev_read(c->dev, t->dev_color, t->w, t->h, t->color) != 0) {
            set_err("readback failed");
            return -1;
        }
        g_stats.readbacks++;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = t->color[i];
    }
    return 0;
}

uint32_t gfx3d_mesh_create(int owner, int usage) {
    int i;
    if (usage < 0 || usage > GFX3D_USAGE_STREAM) {
        usage = GFX3D_USAGE_STATIC;
    }
    for (i = 0; i < MESH_N; ++i) {
        if (!g_mesh[i].live) {
            uint32_t gen = take_gen(g_mesh[i].gen);
            memset(&g_mesh[i], 0, sizeof(g_mesh[i]));
            g_mesh[i].gen = gen;
            g_mesh[i].live = 1;
            g_mesh[i].owner = owner;
            g_mesh[i].usage = usage;
            g_stats.live_mesh++;
            return pack(i, g_mesh[i].gen);
        }
    }
    set_err("mesh table full");
    return 0;
}

static int push_dev_buffer(int owner, uint32_t dev, uint32_t bind, const void *src, int bytes,
                           uint32_t *id) {
    if (!virgl_on()) {
        return 0;
    }
    if (bytes < 0) {
        return -1;
    }
    if (*id) {
        (void)gfx3d_dev_buffer_destroy(owner, dev, *id);
        *id = 0;
    }
    if (bytes == 0) {
        return 0;
    }
    if (gfx3d_dev_buffer(owner, dev, bind, src, (uint32_t)bytes, id) != 0) {
        set_err("buffer upload failed");
        return -1;
    }
    g_stats.uploads++;
    g_stats.bytes_up += (uint32_t)bytes;
    return 0;
}

int gfx3d_mesh_upload(int owner, uint32_t mesh, const Gfx3DLayout *layout, const void *verts,
                      int vbytes, const uint16_t *idx, int nidx) {
    MeshSlot *m = mesh_at(owner, mesh);
    CtxSlot *c;
    int i;
    if (!m || !layout || !verts || vbytes < layout->stride || layout->stride < 4 ||
        layout->nelem < 1 || layout->nelem > 8) {
        return -1;
    }
    mesh_free_buf(m);
    m->verts = (uint8_t *)gmem((uint64_t)vbytes);
    if (!m->verts) {
        set_err("mesh alloc failed");
        return -1;
    }
    memcpy(m->verts, verts, (size_t)vbytes);
    m->vbytes = vbytes;
    m->lay = *layout;
    g_stats.cpu_mesh_bytes += (uint32_t)vbytes;
    if (idx && nidx > 0) {
        m->idx = (uint16_t *)gmem((uint64_t)nidx * sizeof(uint16_t));
        if (!m->idx) {
            return -1;
        }
        for (i = 0; i < nidx; ++i) {
            m->idx[i] = idx[i];
        }
        m->nidx = nidx;
    }
    m->uploaded = 1;
    c = 0;
    for (i = 0; i < CTX_N; ++i) {
        if (g_ctx[i].live && g_ctx[i].owner == owner && g_ctx[i].dev) {
            c = &g_ctx[i];
            break;
        }
    }
    if (c && virgl_on()) {
        m->dev_ctx = c->dev;
        if (push_dev_buffer(owner, c->dev, 1u, m->verts, m->vbytes, &m->dev_vbo) != 0) {
            return -1;
        }
        if (m->idx &&
            push_dev_buffer(owner, c->dev, 2u, m->idx, m->nidx * (int)sizeof(uint16_t), &m->dev_ib) !=
                0) {
            return -1;
        }
    }
    return 0;
}

int gfx3d_mesh_vert(int owner, uint32_t mesh, float x, float y, float z, float nx, float ny, float nz,
                    float u, float v) {
    MeshSlot *m = mesh_at(owner, mesh);
    float *nbuf;
    int cap;
    if (!m) {
        return -1;
    }
    if (m->nacc >= ACC_MAX) {
        set_err("mesh vertex cap");
        return -1;
    }
    if (m->nacc + 1 > m->acc_cap) {
        cap = m->acc_cap == 0 ? 64 : m->acc_cap * 2;
        if (cap < m->nacc + 1) {
            cap = m->nacc + 1;
        }
        if (cap > ACC_MAX) {
            cap = ACC_MAX;
        }
        nbuf = (float *)gmem((uint64_t)cap * 8u * sizeof(float));
        if (!nbuf) {
            return -1;
        }
        if (m->acc && m->nacc > 0) {
            memcpy(nbuf, m->acc, (size_t)m->nacc * 8u * sizeof(float));
        }
        grel(m->acc);
        m->acc = nbuf;
        m->acc_cap = cap;
    }
    m->acc[m->nacc * 8 + 0] = x;
    m->acc[m->nacc * 8 + 1] = y;
    m->acc[m->nacc * 8 + 2] = z;
    m->acc[m->nacc * 8 + 3] = nx;
    m->acc[m->nacc * 8 + 4] = ny;
    m->acc[m->nacc * 8 + 5] = nz;
    m->acc[m->nacc * 8 + 6] = u;
    m->acc[m->nacc * 8 + 7] = v;
    m->nacc++;
    return 0;
}

int gfx3d_mesh_finish(int owner, uint32_t mesh) {
    MeshSlot *m = mesh_at(owner, mesh);
    Gfx3DLayout lay;
    uint8_t *raw;
    int i;
    int bytes;
    if (!m || m->nacc < 3 || !m->acc) {
        return -1;
    }
    bytes = m->nacc * 40;
    raw = (uint8_t *)gmem((uint64_t)bytes);
    if (!raw) {
        return -1;
    }
    memset(raw, 0, (size_t)bytes);
    for (i = 0; i < m->nacc; ++i) {
        float *d = (float *)(raw + i * 40);
        d[0] = m->acc[i * 8 + 0];
        d[1] = m->acc[i * 8 + 1];
        d[2] = m->acc[i * 8 + 2];
        d[3] = 1.0f;
        d[4] = m->acc[i * 8 + 3];
        d[5] = m->acc[i * 8 + 4];
        d[6] = m->acc[i * 8 + 5];
        d[8] = m->acc[i * 8 + 6];
        d[9] = m->acc[i * 8 + 7];
    }
    memset(&lay, 0, sizeof(lay));
    lay.stride = 40;
    lay.nelem = 3;
    lay.location[0] = 0;
    lay.components[0] = 4;
    lay.offset[0] = 0;
    lay.location[1] = 1;
    lay.components[1] = 4;
    lay.offset[1] = 16;
    lay.location[2] = 2;
    lay.components[2] = 2;
    lay.offset[2] = 32;
    if (gfx3d_mesh_upload(owner, mesh, &lay, raw, bytes, 0, 0) != 0) {
        grel(raw);
        return -1;
    }
    grel(raw);
    return 0;
}

int gfx3d_mesh_destroy(int owner, uint32_t mesh) {
    MeshSlot *m = mesh_at(owner, mesh);
    if (!m) {
        return -1;
    }
    release_mesh_dev(m);
    mesh_free_buf(m);
    m->live = 0;
    m->uploaded = 0;
    if (g_stats.live_mesh > 0) {
        g_stats.live_mesh--;
    }
    return 0;
}

uint32_t gfx3d_tex_create(int owner, int w, int h) {
    int i;
    if (w < 1 || h < 1 || w > 1024 || h > 1024) {
        set_err("bad texture");
        return 0;
    }
    for (i = 0; i < TEX_N; ++i) {
        if (!g_tex[i].live) {
            uint32_t gen = take_gen(g_tex[i].gen);
            memset(&g_tex[i], 0, sizeof(g_tex[i]));
            g_tex[i].gen = gen;
            g_tex[i].live = 1;
            g_tex[i].owner = owner;
            g_tex[i].w = w;
            g_tex[i].h = h;
            g_tex[i].pix = (uint32_t *)gmem((uint64_t)w * (uint64_t)h * 4u);
            if (!g_tex[i].pix) {
                g_tex[i].live = 0;
                return 0;
            }
            g_stats.live_tex++;
            return pack(i, g_tex[i].gen);
        }
    }
    set_err("texture table full");
    return 0;
}

int gfx3d_tex_upload(int owner, uint32_t tex, const uint32_t *pixels, int w, int h) {
    TexSlot *t = tex_at(owner, tex);
    int i;
    int ctxi;
    if (!t || !pixels || w != t->w || h != t->h) {
        return -1;
    }
    for (i = 0; i < w * h; ++i) {
        t->pix[i] = pixels[i] | 0xFF000000u;
    }
    for (ctxi = 0; ctxi < CTX_N; ++ctxi) {
        if (g_ctx[ctxi].live && g_ctx[ctxi].owner == owner && g_ctx[ctxi].dev && virgl_on()) {
            t->dev_ctx = g_ctx[ctxi].dev;
            if (t->dev) {
                (void)gfx3d_dev_tex_destroy(owner, t->dev_ctx, t->dev, t->view);
                t->dev = 0;
                t->view = 0;
            }
            if (gfx3d_dev_tex(owner, t->dev_ctx, w, h, t->pix, &t->dev, &t->view) != 0) {
                set_err("texture upload failed");
                return -1;
            }
            g_stats.uploads++;
            g_stats.bytes_up += (uint32_t)w * (uint32_t)h * 4u;
            break;
        }
    }
    return 0;
}

int gfx3d_tex_solid(int owner, uint32_t *out, uint32_t argb) {
    uint32_t h;
    uint32_t px;
    if (!out) {
        return -1;
    }
    h = gfx3d_tex_create(owner, 1, 1);
    if (!h) {
        return -1;
    }
    px = argb | 0xFF000000u;
    if (gfx3d_tex_upload(owner, h, &px, 1, 1) != 0) {
        (void)gfx3d_tex_destroy(owner, h);
        return -1;
    }
    *out = h;
    return 0;
}

int gfx3d_tex_destroy(int owner, uint32_t tex) {
    TexSlot *t = tex_at(owner, tex);
    if (!t) {
        return -1;
    }
    if (t->dev) {
        (void)gfx3d_dev_tex_destroy(owner, t->dev_ctx, t->dev, t->view);
    }
    grel(t->pix);
    t->pix = 0;
    t->live = 0;
    if (g_stats.live_tex > 0) {
        g_stats.live_tex--;
    }
    return 0;
}

static int upload_prog(ProgSlot *p, uint32_t dev) {
    const char *vs;
    const char *fs;
    if (p->vs) {
        (void)gfx3d_dev_shader_destroy(dev, p->vs);
        p->vs = 0;
    }
    if (p->fs) {
        (void)gfx3d_dev_shader_destroy(dev, p->fs);
        p->fs = 0;
    }
    vs = sh_program_tgsi(p->prog, SH_STAGE_VERTEX);
    fs = sh_program_tgsi(p->prog, SH_STAGE_FRAGMENT);
    if (!vs || !fs) {
        return -1;
    }
    if (gfx3d_dev_shader(dev, SH_STAGE_VERTEX, vs, &p->vs) != 0 ||
        gfx3d_dev_shader(dev, SH_STAGE_FRAGMENT, fs, &p->fs) != 0) {
        set_err("shader submit failed");
        return -1;
    }
    p->stamp = sh_program_gen(p->prog);
    p->dev_ctx = dev;
    return 0;
}

uint32_t gfx3d_prog_prepare(int owner, uint32_t ctx, ShProgram *prog) {
    CtxSlot *c = ctx_at(owner, ctx);
    int i;
    if (!c || !prog || !sh_program_ok(prog)) {
        set_err("program not linked");
        return 0;
    }
    for (i = 0; i < PROG_N; ++i) {
        if (!g_prog[i].live) {
            uint32_t gen = take_gen(g_prog[i].gen);
            memset(&g_prog[i], 0, sizeof(g_prog[i]));
            g_prog[i].gen = gen;
            g_prog[i].live = 1;
            g_prog[i].owner = owner;
            g_prog[i].ctx = ctx;
            g_prog[i].prog = prog;
            g_prog[i].stamp = sh_program_gen(prog);
            if (virgl_on() && c->dev) {
                if (upload_prog(&g_prog[i], c->dev) != 0) {
                    g_prog[i].live = 0;
                    if (g_force == GFX3D_VIRGL) {
                        return 0;
                    }
                    gfx3d_mark_lost();
                }
            }
            g_stats.live_prog++;
            return pack(i, g_prog[i].gen);
        }
    }
    set_err("program table full");
    return 0;
}

int gfx3d_prog_destroy(int owner, uint32_t inst) {
    ProgSlot *p = prog_at(owner, inst);
    if (!p) {
        return -1;
    }
    if (p->vs) {
        (void)gfx3d_dev_shader_destroy(p->dev_ctx, p->vs);
    }
    if (p->fs) {
        (void)gfx3d_dev_shader_destroy(p->dev_ctx, p->fs);
    }
    p->live = 0;
    p->prog = 0;
    if (g_stats.live_prog > 0) {
        g_stats.live_prog--;
    }
    return 0;
}

int gfx3d_layout_ok(const ShProgram *prog, const Gfx3DLayout *layout) {
    int i;
    int n;
    if (!prog || !layout || !sh_program_ok(prog)) {
        return 0;
    }
    n = sh_attrib_count(prog);
    for (i = 0; i < n; ++i) {
        ShAttribInfo info;
        int e;
        int found = 0;
        if (sh_attrib_info(prog, i, &info) != 0) {
            return 0;
        }
        for (e = 0; e < layout->nelem; ++e) {
            if (layout->location[e] == info.location) {
                found = 1;
                if (layout->components[e] < info.ncomp) {
                    return 0;
                }
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

int gfx3d_begin(int owner, uint32_t ctx, uint32_t target) {
    CtxSlot *c = ctx_at(owner, ctx);
    TgtSlot *t = tgt_at(owner, target);
    if (!c || !t || t->ctx != ctx) {
        return -1;
    }
    if (g_force == GFX3D_VIRGL && (g_lost || g_backend != GFX3D_VIRGL)) {
        set_err("forced virgl unavailable");
        return -1;
    }
    c->frame = 1;
    c->target = target;
    c->vp_w = t->w;
    c->vp_h = t->h;
    if (virgl_on() && c->dev) {
        if (gfx3d_dev_frame_begin(c->dev) != 0) {
            set_err("frame begin failed");
            if (g_force == GFX3D_VIRGL) {
                return -1;
            }
            gfx3d_mark_lost();
        }
    }
    if (g_backend == GFX3D_MOCK) {
        log_add("begin\n");
    }
    return 0;
}

int gfx3d_clear(int owner, uint32_t ctx, float r, float g, float b, float a, int depth) {
    CtxSlot *c = ctx_at(owner, ctx);
    TgtSlot *t;
    uint32_t px;
    int i;
    int n;
    if (!c || !c->frame) {
        return -1;
    }
    t = tgt_at(owner, c->target);
    if (!t || !t->color || !t->depth) {
        return -1;
    }
    if (r < 0.0f) {
        r = 0.0f;
    }
    if (g < 0.0f) {
        g = 0.0f;
    }
    if (b < 0.0f) {
        b = 0.0f;
    }
    if (a < 0.0f) {
        a = 0.0f;
    }
    px = ((uint32_t)(a * 255.0f) << 24) | ((uint32_t)(r * 255.0f) << 16) |
         ((uint32_t)(g * 255.0f) << 8) | (uint32_t)(b * 255.0f);
    n = t->w * t->h;
    for (i = 0; i < n; ++i) {
        t->color[i] = px;
        t->depth[i] = 1.0f;
    }
    c->depth_on = depth ? 1 : c->depth_on;
    if (virgl_on() && c->dev) {
        Gfx3DDevDraw d;
        memset(&d, 0, sizeof(d));
        d.color_res = t->dev_color;
        d.depth_res = t->dev_depth;
        d.width = t->w;
        d.height = t->h;
        if (gfx3d_dev_clear(c->dev, &d, r, g, b, a, depth) != 0) {
            return -1;
        }
    }
    if (g_backend == GFX3D_MOCK) {
        log_add("clear\n");
    }
    return 0;
}

int gfx3d_viewport(int owner, uint32_t ctx, int x, int y, int w, int h) {
    CtxSlot *c = ctx_at(owner, ctx);
    (void)x;
    (void)y;
    if (!c || w < 1 || h < 1) {
        return -1;
    }
    c->vp_w = w;
    c->vp_h = h;
    return 0;
}

int gfx3d_depth(int owner, uint32_t ctx, int enable) {
    CtxSlot *c = ctx_at(owner, ctx);
    if (!c) {
        return -1;
    }
    c->depth_on = enable ? 1 : 0;
    return 0;
}

int gfx3d_cull(int owner, uint32_t ctx, int enable) {
    CtxSlot *c = ctx_at(owner, ctx);
    if (!c) {
        return -1;
    }
    c->cull = enable ? 1 : 0;
    return 0;
}

static int upload_named_mat(ShProgram *p, const char *name, const Mat4f *m) {
    float g[16];
    int loc;
    if (!p || !m) {
        return -1;
    }
    loc = sh_uniform_find(p, name);
    if (loc < 0) {
        return 0;
    }
    mat4f_to_glsl(m, g);
    return sh_uniform_set(p, loc, g, 16);
}

int gfx3d_camera(int owner, uint32_t ctx, float x, float y, float z, float yaw, float pitch,
                 float fov_deg, float znear, float zfar) {
    CtxSlot *c = ctx_at(owner, ctx);
    TgtSlot *t;
    float aspect = 1.0f;
    if (!c) {
        return -1;
    }
    math3d_cam_set(x, y, z, yaw, pitch);
    math3d_view(&c->view);
    t = tgt_at(owner, c->target);
    if (t && t->h > 0) {
        aspect = (float)t->w / (float)t->h;
    }
    c->fov = fov_deg;
    c->znear = znear;
    c->zfar = zfar;
    c->have_cam = 1;
    mat4f_perspective(&c->proj, fov_deg, aspect, znear, zfar);
    if (c->prog) {
        ProgSlot *p = prog_at(owner, c->prog);
        if (p && p->prog) {
            (void)upload_named_mat(p->prog, "view", &c->view);
            (void)upload_named_mat(p->prog, "projection", &c->proj);
        }
    }
    return 0;
}

int gfx3d_model(int owner, uint32_t ctx, const Mat4f *model) {
    CtxSlot *c = ctx_at(owner, ctx);
    if (!c || !model) {
        return -1;
    }
    c->model = *model;
    if (c->prog) {
        ProgSlot *p = prog_at(owner, c->prog);
        if (p && p->prog) {
            (void)upload_named_mat(p->prog, "model", model);
            {
                float n9[9];
                int loc = sh_uniform_find(p->prog, "normalMatrix");
                if (loc >= 0) {
                    mat4f_normal3(model, n9);
                    (void)sh_uniform_set(p->prog, loc, n9, 9);
                }
            }
        }
    }
    return 0;
}

int gfx3d_use(int owner, uint32_t ctx, uint32_t inst) {
    CtxSlot *c = ctx_at(owner, ctx);
    ProgSlot *p = prog_at(owner, inst);
    if (!c || !p || p->ctx != ctx) {
        return -1;
    }
    if (p->prog && sh_program_gen(p->prog) != p->stamp && virgl_on() && c->dev) {
        if (upload_prog(p, c->dev) != 0) {
            return -1;
        }
    }
    c->prog = inst;
    if (g_backend == GFX3D_MOCK) {
        log_add("use\n");
    }
    return 0;
}

int gfx3d_uniform_mat4(int owner, uint32_t ctx, int loc, const Mat4f *m) {
    CtxSlot *c = ctx_at(owner, ctx);
    ProgSlot *p;
    float g[16];
    if (!c || !m) {
        return -1;
    }
    p = prog_at(owner, c->prog);
    if (!p || !p->prog) {
        return -1;
    }
    mat4f_to_glsl(m, g);
    return sh_uniform_set(p->prog, loc, g, 16);
}

int gfx3d_uniform_f(int owner, uint32_t ctx, int loc, const float *v, int n) {
    CtxSlot *c = ctx_at(owner, ctx);
    ProgSlot *p;
    if (!c || !v || n < 1) {
        return -1;
    }
    p = prog_at(owner, c->prog);
    if (!p || !p->prog) {
        return -1;
    }
    return sh_uniform_set(p->prog, loc, v, n);
}

int gfx3d_bind_tex(int owner, uint32_t ctx, int unit, uint32_t tex) {
    CtxSlot *c = ctx_at(owner, ctx);
    (void)unit;
    if (!c || (tex && !tex_at(owner, tex))) {
        return -1;
    }
    c->tex = tex;
    if (g_backend == GFX3D_MOCK) {
        log_add("tex\n");
    }
    return 0;
}

static float edgef(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void load_attr(const Gfx3DLayout *lay, const uint8_t *vert, float attr[32]) {
    int i;
    int c;
    for (i = 0; i < 32; ++i) {
        attr[i] = 0.0f;
    }
    for (i = 0; i < lay->nelem; ++i) {
        const float *src;
        int loc = lay->location[i];
        if (loc < 0 || loc >= 8 || lay->offset[i] < 0) {
            continue;
        }
        src = (const float *)(vert + lay->offset[i]);
        for (c = 0; c < lay->components[i] && c < 4; ++c) {
            attr[loc * 4 + c] = src[c];
        }
    }
}

static int soft_tri(TgtSlot *t, ShProgram *prog, const TexSlot *tex, const float *a0, const float *a1,
                    const float *a2) {
    float pos[3][4];
    float var[3][8][4];
    float x[3];
    float y[3];
    float invw[3];
    int i;
    int minx, miny, maxx, maxy;
    int ix, iy;
    memset(var, 0, sizeof(var));
    if (sh_soft_vs(prog, a0, pos[0], var[0]) != 0 || sh_soft_vs(prog, a1, pos[1], var[1]) != 0 ||
        sh_soft_vs(prog, a2, pos[2], var[2]) != 0) {
        return -1;
    }
    for (i = 0; i < 3; ++i) {
        if (pos[i][3] > -1e-5f && pos[i][3] < 1e-5f) {
            return 0;
        }
        invw[i] = 1.0f / pos[i][3];
        x[i] = (pos[i][0] * invw[i] * 0.5f + 0.5f) * (float)t->w;
        y[i] = (1.0f - (pos[i][1] * invw[i] * 0.5f + 0.5f)) * (float)t->h;
    }
    minx = (int)x[0];
    maxx = minx;
    miny = (int)y[0];
    maxy = miny;
    for (i = 1; i < 3; ++i) {
        int xi = (int)x[i];
        int yi = (int)y[i];
        if (xi < minx) {
            minx = xi;
        }
        if (yi < miny) {
            miny = yi;
        }
        if (xi > maxx) {
            maxx = xi;
        }
        if (yi > maxy) {
            maxy = yi;
        }
    }
    if (minx < 0) {
        minx = 0;
    }
    if (miny < 0) {
        miny = 0;
    }
    if (maxx >= t->w) {
        maxx = t->w - 1;
    }
    if (maxy >= t->h) {
        maxy = t->h - 1;
    }
    for (iy = miny; iy <= maxy; ++iy) {
        for (ix = minx; ix <= maxx; ++ix) {
            float px = (float)ix + 0.5f;
            float py = (float)iy + 0.5f;
            float den = edgef(x[0], y[0], x[1], y[1], x[2], y[2]);
            float b0, b1, b2, iw, z;
            float vin[8][4];
            float color[4];
            float fc[4];
            int discarded = 0;
            int slot;
            const uint8_t *tp = 0;
            int tw = 0;
            int th = 0;
            if (den > -0.0001f && den < 0.0001f) {
                continue;
            }
            b0 = edgef(x[1], y[1], x[2], y[2], px, py) / den;
            b1 = edgef(x[2], y[2], x[0], y[0], px, py) / den;
            b2 = edgef(x[0], y[0], x[1], y[1], px, py) / den;
            if (b0 < -0.001f || b1 < -0.001f || b2 < -0.001f) {
                continue;
            }
            iw = b0 * invw[0] + b1 * invw[1] + b2 * invw[2];
            if (iw == 0.0f) {
                continue;
            }
            z = (b0 * pos[0][2] * invw[0] + b1 * pos[1][2] * invw[1] + b2 * pos[2][2] * invw[2]) / iw;
            if (z >= t->depth[iy * t->w + ix]) {
                continue;
            }
            memset(vin, 0, sizeof(vin));
            for (slot = 0; slot < 8; ++slot) {
                int comp;
                for (comp = 0; comp < 4; ++comp) {
                    float num = b0 * var[0][slot][comp] * invw[0] + b1 * var[1][slot][comp] * invw[1] +
                                b2 * var[2][slot][comp] * invw[2];
                    vin[slot][comp] = num / iw;
                }
            }
            fc[0] = px;
            fc[1] = py;
            fc[2] = z;
            fc[3] = 1.0f / iw;
            if (tex && tex->pix) {
                tp = (const uint8_t *)tex->pix;
                tw = tex->w;
                th = tex->h;
            }
            if (sh_soft_fs(prog, vin, fc, tp, tw, th, color, &discarded) != 0) {
                return -1;
            }
            if (discarded) {
                continue;
            }
            t->depth[iy * t->w + ix] = z;
            t->color[iy * t->w + ix] = ((uint32_t)(color[3] * 255.0f) << 24) |
                                       ((uint32_t)(color[0] * 255.0f) << 16) |
                                       ((uint32_t)(color[1] * 255.0f) << 8) |
                                       (uint32_t)(color[2] * 255.0f);
        }
    }
    return 0;
}

int gfx3d_draw(int owner, uint32_t ctx, uint32_t mesh) {
    CtxSlot *c = ctx_at(owner, ctx);
    MeshSlot *m;
    ProgSlot *p;
    TgtSlot *t;
    TexSlot *tex = 0;
    int verts;
    int tris = 0;
    int i;
    if (!c || !c->frame) {
        return -1;
    }
    m = mesh_at(owner, mesh);
    p = prog_at(owner, c->prog);
    t = tgt_at(owner, c->target);
    if (!m || !m->uploaded || !p || !p->prog || !t) {
        set_err("draw missing bind");
        return -1;
    }
    if (!gfx3d_layout_ok(p->prog, &m->lay)) {
        set_err("vertex layout mismatch");
        return -1;
    }
    if (c->tex) {
        tex = tex_at(owner, c->tex);
    }
    verts = m->lay.stride > 0 ? m->vbytes / m->lay.stride : 0;
    if (m->nidx > 0) {
        tris = m->nidx / 3;
    } else {
        tris = verts / 3;
    }
    if (tris < 1) {
        return -1;
    }
    if (virgl_on() && c->dev) {
        Gfx3DDevDraw d;
        if (p->prog && sh_program_gen(p->prog) != p->stamp) {
            if (upload_prog(p, c->dev) != 0) {
                return -1;
            }
        }
        if (!m->dev_vbo) {
            m->dev_ctx = c->dev;
            if (push_dev_buffer(owner, c->dev, 1u, m->verts, m->vbytes, &m->dev_vbo) != 0) {
                return -1;
            }
            if (m->idx && push_dev_buffer(owner, c->dev, 2u, m->idx, m->nidx * (int)sizeof(uint16_t),
                                          &m->dev_ib) != 0) {
                return -1;
            }
        }
        memset(&d, 0, sizeof(d));
        d.vs = p->vs;
        d.fs = p->fs;
        d.prog = p->prog;
        d.vbo = m->dev_vbo;
        d.ib = m->dev_ib;
        d.stride = (uint32_t)m->lay.stride;
        d.count = m->nidx > 0 ? (uint32_t)m->nidx : (uint32_t)verts;
        d.indexed = m->nidx > 0;
        d.depth = c->depth_on;
        d.cull = c->cull;
        d.textured = tex && tex->dev ? 1 : 0;
        d.tex = tex ? tex->dev : 0;
        d.view = tex ? tex->view : 0;
        d.nelem = m->lay.nelem;
        d.color_res = t->dev_color;
        d.depth_res = t->dev_depth;
        d.width = t->w;
        d.height = t->h;
        for (i = 0; i < m->lay.nelem && i < 8; ++i) {
            d.off[i] = (uint32_t)m->lay.offset[i];
            if (m->lay.components[i] == 2) {
                d.fmt[i] = 29u;
            } else {
                d.fmt[i] = 31u;
            }
        }
        if (gfx3d_dev_draw(c->dev, &d) != 0) {
            set_err("virgl draw failed");
            if (g_force == GFX3D_VIRGL) {
                return -1;
            }
            gfx3d_mark_lost();
        }
    } else if (g_backend != GFX3D_MOCK) {
        for (i = 0; i < tris; ++i) {
            int i0, i1, i2;
            float a0[32], a1[32], a2[32];
            if (m->nidx > 0) {
                i0 = m->idx[i * 3 + 0];
                i1 = m->idx[i * 3 + 1];
                i2 = m->idx[i * 3 + 2];
            } else {
                i0 = i * 3;
                i1 = i0 + 1;
                i2 = i0 + 2;
            }
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= verts || i1 >= verts || i2 >= verts) {
                return -1;
            }
            load_attr(&m->lay, m->verts + i0 * m->lay.stride, a0);
            load_attr(&m->lay, m->verts + i1 * m->lay.stride, a1);
            load_attr(&m->lay, m->verts + i2 * m->lay.stride, a2);
            if (soft_tri(t, p->prog, tex, a0, a1, a2) != 0) {
                return -1;
            }
        }
    }
    g_stats.draws++;
    g_stats.triangles += (uint32_t)tris;
    if (g_backend == GFX3D_MOCK) {
        log_add("draw\n");
    }
    return 0;
}

int gfx3d_end(int owner, uint32_t ctx) {
    CtxSlot *c = ctx_at(owner, ctx);
    if (!c || !c->frame) {
        return -1;
    }
    if (virgl_on() && c->dev) {
        if (gfx3d_dev_frame_end(c->dev) != 0) {
            set_err("submit failed");
            if (g_force == GFX3D_VIRGL) {
                return -1;
            }
            gfx3d_mark_lost();
        }
        {
            uint32_t sub = gfx3d_dev_submits();
            uint32_t dw = gfx3d_dev_dwords();
            if (sub >= g_seen_sub) {
                g_stats.submits += sub - g_seen_sub;
            }
            if (dw >= g_seen_dw) {
                g_stats.dwords += dw - g_seen_dw;
            }
            g_seen_sub = sub;
            g_seen_dw = dw;
        }
        g_stats.gpu_backing_bytes = gfx3d_dev_backing();
        gfx3d_dev_obj_counts(&g_stats.virgl_obj_live, &g_stats.virgl_obj_peak);
    }
    c->frame = 0;
    if (g_backend == GFX3D_MOCK) {
        log_add("end\n");
    }
    return 0;
}

int gfx3d_scanout_primary(void) {
#ifdef __freestanding__
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t id = vgpu_primary_res();
    vgpu_fb_size(&w, &h);
    if (id == 0u || w == 0u || h == 0u) {
        return -1;
    }
    return vgpu_set_scanout(0, id, 0, 0, w, h);
#else
    return -1;
#endif
}

int gfx3d_present_scanout(int owner, uint32_t target) {
    TgtSlot *t = tgt_at(owner, target);
    if (!t) {
        return -1;
    }
    if (!virgl_on() || !t->dev_color) {
        return -1;
    }
    return gfx3d_dev_scanout(t->dev_color, t->w, t->h);
}

void gfx3d_drop_owner(int owner) {
    int i;
    for (i = 0; i < MESH_N; ++i) {
        if (g_mesh[i].live && g_mesh[i].owner == owner) {
            (void)gfx3d_mesh_destroy(owner, pack(i, g_mesh[i].gen));
        }
    }
    for (i = 0; i < TEX_N; ++i) {
        if (g_tex[i].live && g_tex[i].owner == owner) {
            (void)gfx3d_tex_destroy(owner, pack(i, g_tex[i].gen));
        }
    }
    for (i = 0; i < PROG_N; ++i) {
        if (g_prog[i].live && g_prog[i].owner == owner) {
            (void)gfx3d_prog_destroy(owner, pack(i, g_prog[i].gen));
        }
    }
    for (i = 0; i < TGT_N; ++i) {
        if (g_tgt[i].live && g_tgt[i].owner == owner) {
            (void)gfx3d_target_destroy(owner, pack(i, g_tgt[i].gen));
        }
    }
    for (i = 0; i < CTX_N; ++i) {
        if (g_ctx[i].live && g_ctx[i].owner == owner) {
            (void)gfx3d_context_destroy(owner, pack(i, g_ctx[i].gen));
        }
    }
    if (g_imp_owner == owner) {
        g_implicit = 0;
        g_imp_owner = -1;
    }
}

void gfx3d_stats(Gfx3DStats *out) {
    if (!out) {
        return;
    }
    if (virgl_on()) {
        gfx3d_dev_obj_counts(&g_stats.virgl_obj_live, &g_stats.virgl_obj_peak);
        g_stats.gpu_backing_bytes = gfx3d_dev_backing();
    }
    g_stats.backend = g_backend;
    g_stats.forced = g_force;
    *out = g_stats;
}

void gfx3d_stats_reset(void) {
    int live_m = g_stats.live_mesh;
    int live_t = g_stats.live_tex;
    int live_g = g_stats.live_target;
    int live_p = g_stats.live_prog;
    int live_c = g_stats.live_ctx;
    uint32_t cpu = g_stats.cpu_mesh_bytes;
    uint32_t tgt = g_stats.target_bytes;
    uint32_t dep = g_stats.depth_bytes;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.live_mesh = live_m;
    g_stats.live_tex = live_t;
    g_stats.live_target = live_g;
    g_stats.live_prog = live_p;
    g_stats.live_ctx = live_c;
    g_stats.cpu_mesh_bytes = cpu;
    g_stats.target_bytes = tgt;
    g_stats.depth_bytes = dep;
}

int gfx3d_present_window(int owner, uint32_t *pixels, int w, int h) {
    TgtSlot *t;
    int i;
    int n;
    int copy_w;
    int copy_h;
    int y;
    if (!g_implicit || g_imp_owner != owner || !pixels) {
        return 1;
    }
    if (gfx3d_end(owner, g_imp_ctx) != 0) {
        g_implicit = 0;
        return g_force == GFX3D_VIRGL ? -1 : 1;
    }
    t = tgt_at(owner, g_imp_tgt);
    if (!t) {
        g_implicit = 0;
        return 1;
    }
    if (virgl_on()) {
        if (gfx3d_target_read(owner, g_imp_tgt, t->color, t->w * t->h) != 0) {
            g_implicit = 0;
            return -1;
        }
    }
    copy_w = t->w < w ? t->w : w;
    copy_h = t->h < h ? t->h : h;
    for (y = 0; y < copy_h; ++y) {
        for (i = 0; i < copy_w; ++i) {
            pixels[y * w + i] = t->color[y * t->w + i];
        }
    }
    n = copy_w * copy_h;
    (void)n;
    g_implicit = 0;
    return 0;
}

int gfx3d_world_draw(int owner, uint32_t *pixels, int w, int h) {
    (void)owner;
    (void)pixels;
    (void)w;
    (void)h;
    return 1;
}

int gfx3d_meshf_draw(int owner, const float *pos, int vertices, const int32_t *idx, int triangles,
                     float ox, float oy, float oz, float yaw, int color, uint32_t *pixels, int w,
                     int h) {
    (void)owner;
    (void)pos;
    (void)vertices;
    (void)idx;
    (void)triangles;
    (void)ox;
    (void)oy;
    (void)oz;
    (void)yaw;
    (void)color;
    (void)pixels;
    (void)w;
    (void)h;
    return 1;
}

#ifndef __freestanding__
int gfx3d_dev_available(void) {
    return 0;
}
int gfx3d_dev_ctx(int owner, uint32_t *ctx) {
    (void)owner;
    (void)ctx;
    return -1;
}
int gfx3d_dev_ctx_destroy(int owner, uint32_t ctx) {
    (void)owner;
    (void)ctx;
    return -1;
}
int gfx3d_dev_target(int owner, uint32_t ctx, int w, int h, uint32_t *color, uint32_t *depth, int *dma) {
    (void)owner;
    (void)ctx;
    (void)w;
    (void)h;
    (void)color;
    (void)depth;
    (void)dma;
    return -1;
}
int gfx3d_dev_target_destroy(int owner, uint32_t ctx, uint32_t color, uint32_t depth, int dma) {
    (void)owner;
    (void)ctx;
    (void)color;
    (void)depth;
    (void)dma;
    return -1;
}
int gfx3d_dev_read(uint32_t ctx, uint32_t color, int w, int h, uint32_t *dst) {
    (void)ctx;
    (void)color;
    (void)w;
    (void)h;
    (void)dst;
    return -1;
}
int gfx3d_dev_buffer(int owner, uint32_t ctx, uint32_t bind, const void *src, uint32_t bytes, uint32_t *id) {
    (void)owner;
    (void)ctx;
    (void)bind;
    (void)src;
    (void)bytes;
    (void)id;
    return -1;
}
int gfx3d_dev_buffer_destroy(int owner, uint32_t ctx, uint32_t id) {
    (void)owner;
    (void)ctx;
    (void)id;
    return 0;
}
int gfx3d_dev_tex(int owner, uint32_t ctx, int w, int h, const uint32_t *bgra, uint32_t *id, uint32_t *view) {
    (void)owner;
    (void)ctx;
    (void)w;
    (void)h;
    (void)bgra;
    (void)id;
    (void)view;
    return -1;
}
int gfx3d_dev_tex_destroy(int owner, uint32_t ctx, uint32_t id, uint32_t view) {
    (void)owner;
    (void)ctx;
    (void)id;
    (void)view;
    return 0;
}
int gfx3d_dev_shader(uint32_t ctx, int stage, const char *tgsi, uint32_t *handle) {
    (void)ctx;
    (void)stage;
    (void)tgsi;
    (void)handle;
    return -1;
}
int gfx3d_dev_shader_destroy(uint32_t ctx, uint32_t handle) {
    (void)ctx;
    (void)handle;
    return 0;
}
int gfx3d_dev_frame_begin(uint32_t ctx) {
    (void)ctx;
    return -1;
}
int gfx3d_dev_clear(uint32_t ctx, const Gfx3DDevDraw *d, float r, float g, float b, float a, int depth) {
    (void)ctx;
    (void)d;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    (void)depth;
    return -1;
}
int gfx3d_dev_draw(uint32_t ctx, const Gfx3DDevDraw *d) {
    (void)ctx;
    (void)d;
    return -1;
}
int gfx3d_dev_frame_end(uint32_t ctx) {
    (void)ctx;
    return -1;
}
int gfx3d_dev_scanout(uint32_t color, int w, int h) {
    (void)color;
    (void)w;
    (void)h;
    return -1;
}
void gfx3d_dev_obj_counts(int *live, int *peak) {
    if (live) {
        *live = 0;
    }
    if (peak) {
        *peak = 0;
    }
}
uint32_t gfx3d_dev_submits(void) {
    return 0;
}
uint32_t gfx3d_dev_dwords(void) {
    return 0;
}
uint32_t gfx3d_dev_backing(void) {
    return 0;
}
#endif
