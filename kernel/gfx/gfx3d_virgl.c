#include "gfx3d_dev.h"

#include "gfx3d_batch.h"
#include "serial.h"
#include "vgpu.h"
#include "virgl_obj.h"
#include "virgl_proto.h"

#include <string.h>

/*
 * VirGL device half of Gfx3D.
 *
 * VirtIO resource ids, VirGL object handles, and ChrisOS public handles stay
 * distinct. Buffers and textures take slices of one persistent DMA slab.
 * Color targets use their own DMA because readback maps that backing.
 * Depth targets have no guest backing. Submits are synchronous, so a frame
 * fence is recorded but staging is not reused until the submit returns.
 */

#define DEV_N 8
#define VE_N 8
#define SLAB_PAGES 512
#define SLAB_SLICES 64
#define SLAB_ALIGN 256u
#define TRACK_N 96
#define DRAW_RESERVE 320u

enum {
    KIND_SLAB = 1,
    KIND_DEDICATED = 2,
    KIND_NONE = 3
};

typedef struct Slice {
    uint32_t off;
    uint32_t bytes;
    uint8_t used;
} Slice;

typedef struct Track {
    int live;
    int owner;
    uint32_t ctx;
    uint32_t id;
    int kind;
    int dma;
    uint32_t off;
    uint32_t bytes;
} Track;

typedef struct VeSlot {
    int live;
    uint32_t handle;
    int nelem;
    uint32_t off[8];
    uint32_t fmt[8];
} VeSlot;

typedef struct DevCtx {
    int live;
    int owner;
    uint32_t ctx;
    VirglObjPool objs;
    Gfx3DBatch batch;
    int pipe;
    uint32_t blend;
    uint32_t dsa;
    uint32_t dsa_off;
    uint32_t rs;
    uint32_t rs_cull;
    uint32_t samp;
    VeSlot ve[VE_N];
    uint32_t surf_color;
    uint32_t surf_depth;
    uint32_t color_res;
    uint32_t depth_res;
    int vp_set;
    int vp_w;
    int vp_h;
    int framing;
    uint32_t frame_id;
    uint32_t fence_id;
} DevCtx;

static DevCtx g_dev[DEV_N];
static Track g_track[TRACK_N];
static Slice g_slice[SLAB_SLICES];
static int g_slice_n;
static int g_slab_dma = -1;
static uint32_t g_slab_cap;
static uint32_t g_slab_used;
static uint32_t g_ded_bytes;
static uint32_t g_submits;
static uint32_t g_dwords;
static int g_obj_peak;

static uint32_t fbits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static void note_objs(void) {
    int live = 0;
    int i;
    for (i = 0; i < DEV_N; ++i) {
        if (g_dev[i].live) {
            live += virgl_obj_live_count(&g_dev[i].objs);
        }
    }
    if (live > g_obj_peak) {
        g_obj_peak = live;
    }
}

static DevCtx *dev_at(uint32_t ctx) {
    int i;
    if (ctx == 0u) {
        return 0;
    }
    for (i = 0; i < DEV_N; ++i) {
        if (g_dev[i].live && g_dev[i].ctx == ctx) {
            return &g_dev[i];
        }
    }
    return 0;
}

static uint32_t pick_capset(void) {
    uint32_t i;
    uint32_t fallback = 0;
    for (i = 0; i < vgpu_capset_n(); ++i) {
        uint32_t id = 0;
        if (vgpu_capset_at(i, &id, 0, 0, 0) != 0) {
            continue;
        }
        if (id == VGPU_CAPSET_VIRGL2) {
            return id;
        }
        if (id == VGPU_CAPSET_VIRGL) {
            fallback = id;
        }
    }
    return fallback;
}

static int submit_raw(uint32_t ctx, const uint32_t *d, uint32_t n) {
    if (n == 0u) {
        return 0;
    }
    if (vgpu_submit3d(ctx, d, n) != 0) {
        return -1;
    }
    g_submits++;
    g_dwords += n;
    return 0;
}

static int batch_submit(void *user, uint32_t ctx, const uint32_t *d, uint32_t n) {
    (void)user;
    return submit_raw(ctx, d, n);
}

static int flush_dev(DevCtx *dc) {
    if (!dc) {
        return -1;
    }
    return gfx3d_batch_flush(&dc->batch);
}

static Track *track_find(uint32_t id) {
    int i;
    for (i = 0; i < TRACK_N; ++i) {
        if (g_track[i].live && g_track[i].id == id) {
            return &g_track[i];
        }
    }
    return 0;
}

static Track *track_add(int owner, uint32_t ctx, uint32_t id, int kind, int dma, uint32_t off,
                        uint32_t bytes) {
    int i;
    for (i = 0; i < TRACK_N; ++i) {
        if (!g_track[i].live) {
            g_track[i].live = 1;
            g_track[i].owner = owner;
            g_track[i].ctx = ctx;
            g_track[i].id = id;
            g_track[i].kind = kind;
            g_track[i].dma = dma;
            g_track[i].off = off;
            g_track[i].bytes = bytes;
            return &g_track[i];
        }
    }
    return 0;
}

static void track_del(Track *t) {
    if (t) {
        t->live = 0;
        t->id = 0;
    }
}

static int slab_ensure(void) {
    if (g_slab_dma >= 0) {
        return 0;
    }
    g_slab_dma = vgpu_alloc_dma(SLAB_PAGES);
    if (g_slab_dma < 0) {
        return -1;
    }
    g_slab_cap = (uint32_t)SLAB_PAGES * 4096u;
    g_slice_n = 0;
    g_slab_used = 0;
    return 0;
}

static uint32_t align_up(uint32_t n) {
    return (n + (SLAB_ALIGN - 1u)) & ~(SLAB_ALIGN - 1u);
}

static void slice_remove(int i) {
    int j;
    if (i < 0 || i >= g_slice_n) {
        return;
    }
    for (j = i; j + 1 < g_slice_n; ++j) {
        g_slice[j] = g_slice[j + 1];
    }
    g_slice_n--;
}

static int slab_alloc(uint32_t bytes, uint32_t *off) {
    uint32_t need;
    int i;
    uint32_t bump;
    if (!off || bytes == 0u) {
        return -1;
    }
    need = align_up(bytes);
    if (slab_ensure() != 0) {
        return -1;
    }
    for (i = 0; i < g_slice_n; ++i) {
        if (!g_slice[i].used && g_slice[i].bytes >= need) {
            if (g_slice[i].bytes > need && g_slice_n < SLAB_SLICES) {
                int j;
                for (j = g_slice_n; j > i + 1; --j) {
                    g_slice[j] = g_slice[j - 1];
                }
                g_slice[i + 1].off = g_slice[i].off + need;
                g_slice[i + 1].bytes = g_slice[i].bytes - need;
                g_slice[i + 1].used = 0;
                g_slice[i].bytes = need;
                g_slice_n++;
            }
            g_slice[i].used = 1;
            *off = g_slice[i].off;
            g_slab_used += g_slice[i].bytes;
            return 0;
        }
    }
    bump = 0;
    if (g_slice_n > 0) {
        bump = g_slice[g_slice_n - 1].off + g_slice[g_slice_n - 1].bytes;
    }
    if (g_slice_n >= SLAB_SLICES || bump + need > g_slab_cap) {
        return -1;
    }
    g_slice[g_slice_n].off = bump;
    g_slice[g_slice_n].bytes = need;
    g_slice[g_slice_n].used = 1;
    *off = bump;
    g_slab_used += need;
    g_slice_n++;
    return 0;
}

static void slab_free(uint32_t off) {
    int i;
    for (i = 0; i < g_slice_n; ++i) {
        if (g_slice[i].used && g_slice[i].off == off) {
            if (g_slab_used >= g_slice[i].bytes) {
                g_slab_used -= g_slice[i].bytes;
            } else {
                g_slab_used = 0;
            }
            g_slice[i].used = 0;
            if (i + 1 < g_slice_n && !g_slice[i + 1].used) {
                g_slice[i].bytes += g_slice[i + 1].bytes;
                slice_remove(i + 1);
            }
            if (i > 0 && !g_slice[i - 1].used) {
                g_slice[i - 1].bytes += g_slice[i].bytes;
                slice_remove(i);
            }
            return;
        }
    }
}

static void copy_to(int dma, uint32_t off, const void *src, uint32_t bytes) {
    uint8_t *dst = vgpu_dma_ptr(dma);
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;
    if (!dst || !s) {
        return;
    }
    dst += off;
    for (i = 0; i < bytes; ++i) {
        dst[i] = s[i];
    }
}

static int xfer_buf(uint32_t ctx, uint32_t id, uint32_t bytes) {
    VgpuXfer3D box;
    box.resource_id = id;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = bytes;
    box.h = 1;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = 0;
    box.layer_stride = 0;
    return vgpu_xfer3d(1, ctx, &box);
}

static int xfer_tex(uint32_t ctx, uint32_t id, uint32_t w, uint32_t h) {
    VgpuXfer3D box;
    box.resource_id = id;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = w;
    box.h = h;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = w * 4u;
    box.layer_stride = w * h * 4u;
    return vgpu_xfer3d(1, ctx, &box);
}

static void release_res(Track *t) {
    if (!t || !t->live) {
        return;
    }
    if (t->ctx != 0u) {
        (void)vgpu_ctx_detach(t->ctx, t->id);
    }
    if (t->kind == KIND_DEDICATED) {
        (void)vgpu_res_drop(t->owner, t->id);
        if (g_ded_bytes >= t->bytes) {
            g_ded_bytes -= t->bytes;
        } else {
            g_ded_bytes = 0;
        }
    } else {
        (void)vgpu_res_unref(t->owner, t->id);
        if (t->kind == KIND_SLAB) {
            slab_free(t->off);
        }
    }
    track_del(t);
}

static int make_buffer(int owner, uint32_t ctx, uint32_t bind, const void *src, uint32_t bytes,
                       uint32_t *id) {
    VgpuCreate3D info;
    uint32_t off = 0;
    uint32_t rid = 0;
    Track *tr;
    if (slab_alloc(bytes, &off) != 0) {
        return -1;
    }
    copy_to(g_slab_dma, off, src, bytes);
    memset(&info, 0, sizeof info);
    info.target = VIRGL_TARGET_BUFFER;
    info.format = VIRGL_FORMAT_R8_UNORM;
    info.bind = bind;
    info.width = bytes;
    info.height = 1;
    info.depth = 1;
    info.array_size = 1;
    if (vgpu_res_create_3d_off(owner, &info, g_slab_dma, off, bytes, &rid) != 0 ||
        vgpu_ctx_attach(ctx, rid) != 0 || xfer_buf(ctx, rid, bytes) != 0) {
        if (rid != 0u) {
            (void)vgpu_ctx_detach(ctx, rid);
            (void)vgpu_res_unref(owner, rid);
        }
        slab_free(off);
        return -1;
    }
    tr = track_add(owner, ctx, rid, KIND_SLAB, g_slab_dma, off, bytes);
    if (!tr) {
        (void)vgpu_ctx_detach(ctx, rid);
        (void)vgpu_res_unref(owner, rid);
        slab_free(off);
        return -1;
    }
    *id = rid;
    return 0;
}

static int emit_now(DevCtx *dc, int (*fill)(VirglCmd *, void *), void *user) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    if (flush_dev(dc) != 0) {
        return -1;
    }
    if (virgl_cmd_init(&c, buf, VIRGL_CMD_MAX, dc->ctx) != 0 || fill(&c, user) != 0 ||
        !virgl_cmd_ok(&c)) {
        return -1;
    }
    return submit_raw(dc->ctx, buf, virgl_cmd_len(&c));
}

struct PipeArgs {
    DevCtx *dc;
};

static int fill_pipe(VirglCmd *c, void *user) {
    struct PipeArgs *a = (struct PipeArgs *)user;
    DevCtx *dc = a->dc;
    dc->blend = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_BLEND);
    dc->dsa = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_DSA);
    dc->dsa_off = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_DSA);
    dc->rs = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_RASTERIZER);
    dc->rs_cull = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_RASTERIZER);
    dc->samp = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_SAMPLER_STATE);
    if (dc->blend == 0u || dc->dsa == 0u || dc->dsa_off == 0u || dc->rs == 0u ||
        dc->rs_cull == 0u || dc->samp == 0u) {
        return -1;
    }
    if (virgl_cmd_blend_opaque(c, dc->blend) != 0 || virgl_cmd_dsa(c, dc->dsa, 1, 1u) != 0 ||
        virgl_cmd_dsa(c, dc->dsa_off, 0, 7u) != 0 || virgl_cmd_raster(c, dc->rs, 0u) != 0 ||
        virgl_cmd_raster(c, dc->rs_cull, 2u) != 0 || virgl_cmd_sampler(c, dc->samp) != 0) {
        return -1;
    }
    note_objs();
    return 0;
}

static int ensure_pipe(DevCtx *dc) {
    struct PipeArgs a;
    if (dc->pipe) {
        return 0;
    }
    a.dc = dc;
    if (emit_now(dc, fill_pipe, &a) != 0) {
        return -1;
    }
    dc->pipe = 1;
    return 0;
}

static int emit_vp(DevCtx *dc, int w, int h) {
    VirglCmd *c;
    if (w < 1 || h < 1) {
        return -1;
    }
    if (dc->vp_set && dc->vp_w == w && dc->vp_h == h) {
        return 0;
    }
    if (gfx3d_batch_reserve(&dc->batch, 16u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    if (virgl_cmd_viewport(c, fbits((float)w * 0.5f), fbits((float)h * 0.5f), VIRGL_F32_HALF,
                           fbits((float)w * 0.5f), fbits((float)h * 0.5f), VIRGL_F32_HALF) != 0) {
        return -1;
    }
    dc->vp_set = 1;
    dc->vp_w = w;
    dc->vp_h = h;
    return 0;
}

static int destroy_obj_batch(DevCtx *dc, uint32_t type, uint32_t handle) {
    VirglCmd *c;
    if (handle == 0u) {
        return 0;
    }
    if (gfx3d_batch_reserve(&dc->batch, 4u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    if (virgl_cmd_destroy(c, type, handle) != 0) {
        return -1;
    }
    (void)virgl_obj_free(&dc->objs, handle);
    note_objs();
    return 0;
}

static int ensure_fb(DevCtx *dc, const Gfx3DDevDraw *d, int with_depth) {
    VirglCmd *c;
    uint32_t zs = 0;
    int need_color;
    int need_depth;
    if (!d || d->color_res == 0u) {
        return -1;
    }
    need_color = dc->surf_color == 0u || dc->color_res != d->color_res;
    need_depth = with_depth && d->depth_res != 0u &&
                 (dc->surf_depth == 0u || dc->depth_res != d->depth_res);
    if (!with_depth && dc->surf_depth != 0u && dc->depth_res != 0u) {
        need_depth = 0;
    }
    if (need_color || (with_depth && d->depth_res != dc->depth_res)) {
        if (dc->surf_color != 0u && destroy_obj_batch(dc, VIRGL_OBJECT_SURFACE, dc->surf_color) != 0) {
            return -1;
        }
        dc->surf_color = 0;
        dc->color_res = 0;
        if (dc->surf_depth != 0u && destroy_obj_batch(dc, VIRGL_OBJECT_SURFACE, dc->surf_depth) != 0) {
            return -1;
        }
        dc->surf_depth = 0;
        dc->depth_res = 0;
        need_color = 1;
        need_depth = with_depth && d->depth_res != 0u;
    }
    if (gfx3d_batch_reserve(&dc->batch, 24u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    if (need_color) {
        dc->surf_color = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_SURFACE);
        if (dc->surf_color == 0u ||
            virgl_cmd_surface(c, dc->surf_color, d->color_res, VIRGL_FORMAT_B8G8R8A8_UNORM) != 0) {
            return -1;
        }
        dc->color_res = d->color_res;
        note_objs();
    }
    if (need_depth) {
        dc->surf_depth = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_SURFACE);
        if (dc->surf_depth == 0u ||
            virgl_cmd_surface(c, dc->surf_depth, d->depth_res, VIRGL_FORMAT_Z32_FLOAT) != 0) {
            return -1;
        }
        dc->depth_res = d->depth_res;
        note_objs();
    }
    if (with_depth && dc->surf_depth != 0u && dc->depth_res == d->depth_res) {
        zs = dc->surf_depth;
    }
    return virgl_cmd_framebuffer(c, 1u, zs, dc->surf_color);
}

static int ve_match(const VeSlot *s, const Gfx3DDevDraw *d) {
    int i;
    if (!s->live || s->nelem != d->nelem) {
        return 0;
    }
    for (i = 0; i < d->nelem && i < 8; ++i) {
        if (s->off[i] != d->off[i] || s->fmt[i] != d->fmt[i]) {
            return 0;
        }
    }
    return 1;
}

static int ensure_ve(DevCtx *dc, const Gfx3DDevDraw *d, uint32_t *handle) {
    int i;
    int slot = -1;
    VirglCmd *c;
    for (i = 0; i < VE_N; ++i) {
        if (ve_match(&dc->ve[i], d)) {
            *handle = dc->ve[i].handle;
            return 0;
        }
    }
    for (i = 0; i < VE_N; ++i) {
        if (!dc->ve[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        if (destroy_obj_batch(dc, VIRGL_OBJECT_VERTEX_ELEMENTS, dc->ve[0].handle) != 0) {
            return -1;
        }
        dc->ve[0].live = 0;
        dc->ve[0].handle = 0;
    }
    if (gfx3d_batch_reserve(&dc->batch, 48u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    dc->ve[slot].handle = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_VERTEX_ELEMENTS);
    if (dc->ve[slot].handle == 0u ||
        virgl_cmd_velems(c, dc->ve[slot].handle, (uint32_t)d->nelem, d->off, d->fmt) != 0) {
        return -1;
    }
    dc->ve[slot].live = 1;
    dc->ve[slot].nelem = d->nelem;
    for (i = 0; i < d->nelem && i < 8; ++i) {
        dc->ve[slot].off[i] = d->off[i];
        dc->ve[slot].fmt[i] = d->fmt[i];
    }
    *handle = dc->ve[slot].handle;
    note_objs();
    return 0;
}

static int upload_consts(DevCtx *dc, const ShProgram *prog, int stage) {
    const float *w;
    int nvec = 0;
    uint32_t bits[64];
    int n;
    int i;
    uint32_t gst;
    VirglCmd *c;
    if (!prog) {
        return 0;
    }
    w = sh_uniform_words(prog, stage, &nvec);
    if (nvec <= 0) {
        return 0;
    }
    n = nvec * 4;
    if (!w || n > 64) {
        return -1;
    }
    if (gfx3d_batch_reserve(&dc->batch, (uint32_t)n + 8u) != 0) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        bits[i] = fbits(w[i]);
    }
    gst = stage == SH_STAGE_FRAGMENT ? VIRGL_SHADER_FRAGMENT : VIRGL_SHADER_VERTEX;
    c = gfx3d_batch_cmd(&dc->batch);
    return virgl_cmd_consts(c, gst, bits, (uint32_t)n);
}

static int drop_cached_surface(DevCtx *dc, uint32_t res) {
    if (dc->color_res == res && dc->surf_color != 0u) {
        if (destroy_obj_batch(dc, VIRGL_OBJECT_SURFACE, dc->surf_color) != 0) {
            return -1;
        }
        dc->surf_color = 0;
        dc->color_res = 0;
    }
    if (dc->depth_res == res && dc->surf_depth != 0u) {
        if (destroy_obj_batch(dc, VIRGL_OBJECT_SURFACE, dc->surf_depth) != 0) {
            return -1;
        }
        dc->surf_depth = 0;
        dc->depth_res = 0;
    }
    return flush_dev(dc);
}

int gfx3d_dev_available(void) {
    return vgpu_ready() && vgpu_virgl_on() && pick_capset() != 0u;
}

int gfx3d_dev_ctx(int owner, uint32_t *ctx) {
    uint32_t cap;
    uint32_t id = 0;
    int i;
    int slot = -1;
    if (!ctx) {
        return -1;
    }
    cap = pick_capset();
    if (cap == 0u) {
        return -1;
    }
    for (i = 0; i < DEV_N; ++i) {
        if (!g_dev[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    if (vgpu_ctx_create(owner, cap, 1, &id) != 0) {
        return -1;
    }
    memset(&g_dev[slot], 0, sizeof g_dev[slot]);
    g_dev[slot].live = 1;
    g_dev[slot].owner = owner;
    g_dev[slot].ctx = id;
    virgl_obj_init(&g_dev[slot].objs);
    if (gfx3d_batch_open(&g_dev[slot].batch, id, batch_submit, 0) != 0 ||
        ensure_pipe(&g_dev[slot]) != 0) {
        (void)vgpu_ctx_destroy(owner, id);
        g_dev[slot].live = 0;
        return -1;
    }
    *ctx = id;
    return 0;
}

int gfx3d_dev_ctx_destroy(int owner, uint32_t ctx) {
    DevCtx *dc = dev_at(ctx);
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    int i;
    int ncmd = 0;
    if (!dc || dc->owner != owner) {
        return -1;
    }
    (void)flush_dev(dc);
    if (virgl_cmd_init(&c, buf, VIRGL_CMD_MAX, ctx) == 0) {
        for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
            if (!dc->objs.live[i]) {
                continue;
            }
            if (c.n + 4u >= VIRGL_CMD_MAX) {
                if (ncmd > 0 && virgl_cmd_ok(&c)) {
                    (void)submit_raw(ctx, buf, virgl_cmd_len(&c));
                }
                if (virgl_cmd_init(&c, buf, VIRGL_CMD_MAX, ctx) != 0) {
                    break;
                }
                ncmd = 0;
            }
            if (virgl_cmd_destroy(&c, dc->objs.type[i], dc->objs.handle[i]) == 0) {
                ncmd++;
            }
        }
        if (ncmd > 0 && virgl_cmd_ok(&c)) {
            (void)submit_raw(ctx, buf, virgl_cmd_len(&c));
        }
    }
    for (i = 0; i < TRACK_N; ++i) {
        if (g_track[i].live && g_track[i].ctx == ctx) {
            release_res(&g_track[i]);
        }
    }
    (void)vgpu_ctx_destroy(owner, ctx);
    dc->live = 0;
    note_objs();
    return 0;
}

int gfx3d_dev_target(int owner, uint32_t ctx, int w, int h, uint32_t *color, uint32_t *depth,
                     int *dma) {
    DevCtx *dc = dev_at(ctx);
    VgpuCreate3D info;
    uint32_t bytes;
    int pages;
    int cdma;
    uint32_t cid = 0;
    uint32_t did = 0;
    if (!dc || dc->owner != owner || !color || !depth || !dma || w < 1 || h < 1) {
        return -1;
    }
    bytes = (uint32_t)w * (uint32_t)h * 4u;
    pages = (int)((bytes + 4095u) / 4096u);
    cdma = vgpu_alloc_dma(pages);
    if (cdma < 0) {
        return -1;
    }
    memset(&info, 0, sizeof info);
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
    info.bind = VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SCANOUT | VIRGL_BIND_DISPLAY_TARGET;
    info.width = (uint32_t)w;
    info.height = (uint32_t)h;
    info.depth = 1;
    info.array_size = 1;
    info.flags = 1u;
    if (vgpu_res_create_3d(owner, &info, cdma, bytes, &cid) != 0 || vgpu_ctx_attach(ctx, cid) != 0) {
        if (cid != 0u) {
            (void)vgpu_res_drop(owner, cid);
        } else {
            vgpu_free_dma(cdma);
        }
        return -1;
    }
    memset(&info, 0, sizeof info);
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_Z32_FLOAT;
    info.bind = VIRGL_BIND_DEPTH_STENCIL;
    info.width = (uint32_t)w;
    info.height = (uint32_t)h;
    info.depth = 1;
    info.array_size = 1;
    if (vgpu_res_create_3d(owner, &info, -1, 0, &did) != 0 || vgpu_ctx_attach(ctx, did) != 0) {
        if (did != 0u) {
            (void)vgpu_res_unref(owner, did);
        }
        (void)vgpu_ctx_detach(ctx, cid);
        (void)vgpu_res_drop(owner, cid);
        return -1;
    }
    if (!track_add(owner, ctx, cid, KIND_DEDICATED, cdma, 0, bytes) ||
        !track_add(owner, ctx, did, KIND_NONE, -1, 0, 0)) {
        (void)vgpu_ctx_detach(ctx, did);
        (void)vgpu_res_unref(owner, did);
        (void)vgpu_ctx_detach(ctx, cid);
        (void)vgpu_res_drop(owner, cid);
        return -1;
    }
    g_ded_bytes += bytes;
    *color = cid;
    *depth = did;
    *dma = cdma;
    return 0;
}

int gfx3d_dev_target_destroy(int owner, uint32_t ctx, uint32_t color, uint32_t depth, int dma) {
    DevCtx *dc = dev_at(ctx);
    Track *tc;
    Track *td;
    (void)dma;
    if (!dc || dc->owner != owner) {
        return -1;
    }
    if (drop_cached_surface(dc, color) != 0 || drop_cached_surface(dc, depth) != 0) {
        return -1;
    }
    tc = track_find(color);
    td = track_find(depth);
    if (tc && tc->owner == owner) {
        release_res(tc);
    }
    if (td && td->owner == owner) {
        release_res(td);
    }
    return 0;
}

int gfx3d_dev_read(uint32_t ctx, uint32_t color, int w, int h, uint32_t *dst) {
    DevCtx *dc = dev_at(ctx);
    Track *t;
    VgpuXfer3D box;
    uint32_t *src;
    int n;
    int i;
    if (!dc || !dst || w < 1 || h < 1) {
        return -1;
    }
    if (flush_dev(dc) != 0) {
        return -1;
    }
    t = track_find(color);
    if (!t || t->dma < 0) {
        return -1;
    }
    box.resource_id = color;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = (uint32_t)w;
    box.h = (uint32_t)h;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = (uint32_t)w * 4u;
    box.layer_stride = (uint32_t)w * (uint32_t)h * 4u;
    if (vgpu_xfer3d(0, ctx, &box) != 0) {
        return -1;
    }
    src = (uint32_t *)vgpu_dma_ptr(t->dma);
    if (!src) {
        return -1;
    }
    n = w * h;
    for (i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

int gfx3d_dev_buffer(int owner, uint32_t ctx, uint32_t bind, const void *src, uint32_t bytes,
                     uint32_t *id) {
    DevCtx *dc = dev_at(ctx);
    uint32_t bits;
    if (!dc || dc->owner != owner || !src || !id || bytes == 0u) {
        return -1;
    }
    if (bind == 1u) {
        bits = VIRGL_BIND_VERTEX_BUFFER;
    } else if (bind == 2u) {
        bits = VIRGL_BIND_INDEX_BUFFER;
    } else {
        bits = bind;
    }
    return make_buffer(owner, ctx, bits, src, bytes, id);
}

int gfx3d_dev_buffer_destroy(int owner, uint32_t ctx, uint32_t id) {
    DevCtx *dc = dev_at(ctx);
    Track *t = track_find(id);
    if (!dc || dc->owner != owner || !t || t->owner != owner || t->ctx != ctx) {
        return -1;
    }
    if (flush_dev(dc) != 0) {
        return -1;
    }
    release_res(t);
    return 0;
}

struct ViewArgs {
    uint32_t handle;
    uint32_t res;
};

static int fill_view(VirglCmd *c, void *user) {
    struct ViewArgs *a = (struct ViewArgs *)user;
    return virgl_cmd_sview(c, a->handle, a->res, VIRGL_FORMAT_B8G8R8A8_UNORM);
}

int gfx3d_dev_tex(int owner, uint32_t ctx, int w, int h, const uint32_t *bgra, uint32_t *id,
                  uint32_t *view) {
    DevCtx *dc = dev_at(ctx);
    VgpuCreate3D info;
    uint32_t bytes;
    uint32_t off = 0;
    uint32_t rid = 0;
    uint32_t vh;
    int kind;
    int dma;
    struct ViewArgs args;
    if (!dc || dc->owner != owner || !bgra || !id || !view || w < 1 || h < 1) {
        return -1;
    }
    bytes = (uint32_t)w * (uint32_t)h * 4u;
    if (slab_alloc(bytes, &off) == 0) {
        kind = KIND_SLAB;
        dma = g_slab_dma;
        copy_to(dma, off, bgra, bytes);
        memset(&info, 0, sizeof info);
        info.target = VIRGL_TARGET_TEXTURE_2D;
        info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
        info.bind = VIRGL_BIND_SAMPLER_VIEW;
        info.width = (uint32_t)w;
        info.height = (uint32_t)h;
        info.depth = 1;
        info.array_size = 1;
        info.flags = 1u;
        if (vgpu_res_create_3d_off(owner, &info, dma, off, bytes, &rid) != 0) {
            slab_free(off);
            return -1;
        }
    } else {
        int pages = (int)((bytes + 4095u) / 4096u);
        dma = vgpu_alloc_dma(pages);
        if (dma < 0) {
            return -1;
        }
        kind = KIND_DEDICATED;
        off = 0;
        copy_to(dma, 0, bgra, bytes);
        memset(&info, 0, sizeof info);
        info.target = VIRGL_TARGET_TEXTURE_2D;
        info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
        info.bind = VIRGL_BIND_SAMPLER_VIEW;
        info.width = (uint32_t)w;
        info.height = (uint32_t)h;
        info.depth = 1;
        info.array_size = 1;
        info.flags = 1u;
        if (vgpu_res_create_3d(owner, &info, dma, bytes, &rid) != 0) {
            vgpu_free_dma(dma);
            return -1;
        }
        g_ded_bytes += bytes;
    }
    if (vgpu_ctx_attach(ctx, rid) != 0 || xfer_tex(ctx, rid, (uint32_t)w, (uint32_t)h) != 0 ||
        !track_add(owner, ctx, rid, kind, dma, off, bytes)) {
        if (kind == KIND_SLAB) {
            (void)vgpu_ctx_detach(ctx, rid);
            (void)vgpu_res_unref(owner, rid);
            slab_free(off);
        } else {
            (void)vgpu_ctx_detach(ctx, rid);
            (void)vgpu_res_drop(owner, rid);
            if (g_ded_bytes >= bytes) {
                g_ded_bytes -= bytes;
            }
        }
        return -1;
    }
    vh = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_SAMPLER_VIEW);
    args.handle = vh;
    args.res = rid;
    if (vh == 0u || emit_now(dc, fill_view, &args) != 0) {
        if (vh != 0u) {
            (void)virgl_obj_free(&dc->objs, vh);
        }
        release_res(track_find(rid));
        return -1;
    }
    note_objs();
    *id = rid;
    *view = vh;
    return 0;
}

struct OneObj {
    uint32_t type;
    uint32_t handle;
};

static int fill_destroy(VirglCmd *c, void *user) {
    struct OneObj *o = (struct OneObj *)user;
    return virgl_cmd_destroy(c, o->type, o->handle);
}

int gfx3d_dev_tex_destroy(int owner, uint32_t ctx, uint32_t id, uint32_t view) {
    DevCtx *dc = dev_at(ctx);
    Track *t = track_find(id);
    struct OneObj o;
    if (!dc || dc->owner != owner || !t || t->owner != owner || t->ctx != ctx) {
        return -1;
    }
    if (view != 0u && virgl_obj_live(&dc->objs, view)) {
        o.type = VIRGL_OBJECT_SAMPLER_VIEW;
        o.handle = view;
        if (emit_now(dc, fill_destroy, &o) != 0) {
            return -1;
        }
        (void)virgl_obj_free(&dc->objs, view);
        note_objs();
    }
    release_res(t);
    return 0;
}

struct ShaderArgs {
    uint32_t handle;
    uint32_t stage;
    const char *text;
};

static int fill_shader(VirglCmd *c, void *user) {
    struct ShaderArgs *a = (struct ShaderArgs *)user;
    return virgl_cmd_shader(c, a->handle, a->stage, a->text);
}

int gfx3d_dev_shader(uint32_t ctx, int stage, const char *tgsi, uint32_t *handle) {
    DevCtx *dc = dev_at(ctx);
    struct ShaderArgs a;
    uint32_t h;
    uint32_t gst;
    if (!dc || !tgsi || !handle) {
        return -1;
    }
    if (stage == SH_STAGE_VERTEX) {
        gst = VIRGL_SHADER_VERTEX;
    } else if (stage == SH_STAGE_FRAGMENT) {
        gst = VIRGL_SHADER_FRAGMENT;
    } else {
        return -1;
    }
    h = virgl_obj_alloc(&dc->objs, VIRGL_OBJECT_SHADER);
    if (h == 0u) {
        return -1;
    }
    a.handle = h;
    a.stage = gst;
    a.text = tgsi;
    if (emit_now(dc, fill_shader, &a) != 0) {
        (void)virgl_obj_free(&dc->objs, h);
        return -1;
    }
    note_objs();
    *handle = h;
    return 0;
}

int gfx3d_dev_shader_destroy(uint32_t ctx, uint32_t handle) {
    DevCtx *dc = dev_at(ctx);
    struct OneObj o;
    if (!dc || handle == 0u || !virgl_obj_live(&dc->objs, handle)) {
        return -1;
    }
    o.type = VIRGL_OBJECT_SHADER;
    o.handle = handle;
    if (emit_now(dc, fill_destroy, &o) != 0) {
        return -1;
    }
    (void)virgl_obj_free(&dc->objs, handle);
    note_objs();
    return 0;
}

int gfx3d_dev_frame_begin(uint32_t ctx) {
    DevCtx *dc = dev_at(ctx);
    if (!dc) {
        return -1;
    }
    if (dc->framing) {
        if (flush_dev(dc) != 0) {
            return -1;
        }
    }
    if (ensure_pipe(dc) != 0) {
        return -1;
    }
    dc->framing = 1;
    return 0;
}

int gfx3d_dev_clear(uint32_t ctx, const Gfx3DDevDraw *d, float r, float g, float b, float a,
                    int depth) {
    DevCtx *dc = dev_at(ctx);
    VirglCmd *c;
    if (!dc || !d) {
        return -1;
    }
    if (ensure_pipe(dc) != 0 || ensure_fb(dc, d, depth) != 0 || emit_vp(dc, d->width, d->height) != 0) {
        return -1;
    }
    if (gfx3d_batch_reserve(&dc->batch, 16u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    return virgl_cmd_clear(c, VIRGL_CLEAR_COLOR0 | (depth ? 1u : 0u), fbits(r), fbits(g), fbits(b),
                           fbits(a), 0x3ff0000000000000ull, 0);
}

int gfx3d_dev_draw(uint32_t ctx, const Gfx3DDevDraw *d) {
    DevCtx *dc = dev_at(ctx);
    VirglCmd *c;
    uint32_t ve = 0;
    uint32_t rs;
    if (!dc || !d || d->vs == 0u || d->fs == 0u || d->vbo == 0u || d->nelem < 1) {
        return -1;
    }
    if (ensure_pipe(dc) != 0 || ensure_fb(dc, d, d->depth) != 0 ||
        emit_vp(dc, d->width, d->height) != 0) {
        return -1;
    }
    if (gfx3d_batch_reserve(&dc->batch, DRAW_RESERVE) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    rs = d->cull ? dc->rs_cull : dc->rs;
    if (virgl_cmd_bind(c, VIRGL_OBJECT_BLEND, dc->blend) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_DSA, d->depth ? dc->dsa : dc->dsa_off) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_RASTERIZER, rs) != 0 || virgl_cmd_link(c, d->vs, d->fs) != 0) {
        return -1;
    }
    if (upload_consts(dc, d->prog, SH_STAGE_VERTEX) != 0 ||
        upload_consts(dc, d->prog, SH_STAGE_FRAGMENT) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    if (d->textured && d->view != 0u) {
        if (gfx3d_batch_reserve(&dc->batch, 16u) != 0) {
            return -1;
        }
        c = gfx3d_batch_cmd(&dc->batch);
        if (virgl_cmd_bind_sampler(c, VIRGL_SHADER_FRAGMENT, dc->samp) != 0 ||
            virgl_cmd_set_views(c, VIRGL_SHADER_FRAGMENT, d->view) != 0) {
            return -1;
        }
    }
    if (ensure_ve(dc, d, &ve) != 0) {
        return -1;
    }
    if (gfx3d_batch_reserve(&dc->batch, 32u) != 0) {
        return -1;
    }
    c = gfx3d_batch_cmd(&dc->batch);
    if (virgl_cmd_bind(c, VIRGL_OBJECT_VERTEX_ELEMENTS, ve) != 0 ||
        virgl_cmd_vbuffers(c, d->stride, 0, d->vbo) != 0) {
        return -1;
    }
    if (d->indexed) {
        if (d->ib == 0u || virgl_cmd_ib(c, d->ib, 2u) != 0) {
            return -1;
        }
        return virgl_cmd_draw(c, 0, d->count, 1, 0, 65535u);
    }
    return virgl_cmd_draw(c, 0, d->count, 0, 0, d->count);
}

int gfx3d_dev_frame_end(uint32_t ctx) {
    DevCtx *dc = dev_at(ctx);
    if (!dc) {
        return -1;
    }
    if (flush_dev(dc) != 0) {
        return -1;
    }
    dc->framing = 0;
    dc->frame_id++;
    dc->fence_id++;
    return 0;
}

int gfx3d_dev_scanout(uint32_t color, int w, int h) {
    if (color == 0u || w < 1 || h < 1) {
        return -1;
    }
    if (vgpu_set_scanout(0, color, 0, 0, (uint32_t)w, (uint32_t)h) != 0) {
        return -1;
    }
    return vgpu_res_flush(color, 0, 0, (uint32_t)w, (uint32_t)h);
}

void gfx3d_dev_obj_counts(int *live, int *peak) {
    int n = 0;
    int i;
    for (i = 0; i < DEV_N; ++i) {
        if (g_dev[i].live) {
            n += virgl_obj_live_count(&g_dev[i].objs);
        }
    }
    if (live) {
        *live = n;
    }
    if (peak) {
        *peak = g_obj_peak;
    }
}

uint32_t gfx3d_dev_submits(void) {
    return g_submits;
}

uint32_t gfx3d_dev_dwords(void) {
    return g_dwords;
}

uint32_t gfx3d_dev_backing(void) {
    return g_slab_used + g_ded_bytes;
}
