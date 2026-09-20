/* LEARN:WS64-W08 */
#include "clvm_sys.h"
#include "bench.h"
#include "gfx2d.h"
#include "gfx_fast.h"
#include "gfx_slot.h"
#include "graphics.h"
#include "math3d.h"
#include "mesh.h"
#include "shade.h"
#include "speaker.h"
#include "task.h"
#include "tex.h"
#include "tri.h"
#include "ui.h"
#include "voxel.h"
#include "zbuf.h"

static int pop_i32(ClvmVm *vm, int32_t *out) {
    if (!clvm_vm_pop(vm, out))
        return 0;
    return 1;
}

static float bits_f(int32_t bits) {
    union {
        uint32_t u;
        float f;
    } v;
    v.u = (uint32_t)bits;
    return v.f;
}

static int pop_f(ClvmVm *vm, float *out) {
    int32_t b;
    if (!pop_i32(vm, &b))
        return 0;
    *out = bits_f(b);
    return 1;
}

static int vm_bytes(ClvmVm *vm, int32_t addr, int32_t length, const uint8_t **out) {
    uint32_t a;
    uint32_t n;
    if (addr < 0 || length <= 0)
        return 0;
    a = (uint32_t)addr;
    n = (uint32_t)length;
    if (a >= CLVM_MEMORY_SIZE)
        return 0;
    if (n > CLVM_MEMORY_SIZE - a)
        return 0;
    *out = vm->memory + a;
    return 1;
}

static uint32_t now32(void) {
    extern volatile uint64_t ticks;
    return (uint32_t)ticks;
}

static ClvmGfxCtx *gfx_ctx(ClvmVm *vm, void *user) {
    if (user != 0)
        return (ClvmGfxCtx *)user;
    (void)vm;
    return 0;
}

static void gfx_zbuf_prepare(ClvmGfxCtx *ctx, int gw, int gh) {
    zbuf_set_size(gw, gh);
    if (ctx != 0 && ctx->zbuf != 0)
        zbuf_bind(ctx->zbuf);
    else
        zbuf_bind(0);
}

static void clamp_view(int *w, int *h) {
    if (*w < CLVM_SYS_GAME_W)
        *w = CLVM_SYS_GAME_W;
    if (*h < CLVM_SYS_GAME_H)
        *h = CLVM_SYS_GAME_H;
    if (*w > CLVM_SYS_GAME_MAX_W)
        *w = CLVM_SYS_GAME_MAX_W;
    if (*h > CLVM_SYS_GAME_MAX_H)
        *h = CLVM_SYS_GAME_MAX_H;
}

void clvm_gfx_native_size(int *w, int *h) {
    int nw;
    int nh;
    nw = g_gfx.width;
    nh = g_gfx.height - UI_TASKBAR_HEIGHT - TASK_TITLE_HEIGHT;
    clamp_view(&nw, &nh);
    if (w)
        *w = nw;
    if (h)
        *h = nh;
}

int clvm_gfx_viewport(ClvmGfxCtx *ctx, int w, int h) {
    uint32_t *pix;
    uint32_t *zb;
    int slot;
    int nw;
    int nh;

    if (ctx == 0)
        return -1;
    if (w <= 0 || h <= 0)
        clvm_gfx_native_size(&nw, &nh);
    else {
        nw = w;
        nh = h;
        clamp_view(&nw, &nh);
    }
    if (ctx->slot_id >= 0 && ctx->w == nw && ctx->h == nh && ctx->pixels != 0) {
        math3d_set_screen(nw, nh);
        gfx_zbuf_prepare(ctx, nw, nh);
        return 0;
    }
    pix = 0;
    zb = 0;
    slot = -1;
    if (ctx->slot_id >= 0)
        slot = gfx_slot_resize(ctx->slot_id, nw, nh, &pix, &zb);
    if (slot < 0)
        slot = gfx_slot_alloc(nw, nh, &pix, &zb);
    if (slot < 0 && (nw != CLVM_SYS_GAME_W || nh != CLVM_SYS_GAME_H)) {
        nw = CLVM_SYS_GAME_W;
        nh = CLVM_SYS_GAME_H;
        if (ctx->slot_id >= 0)
            slot = gfx_slot_resize(ctx->slot_id, nw, nh, &pix, &zb);
        if (slot < 0)
            slot = gfx_slot_alloc(nw, nh, &pix, &zb);
    }
    if (slot < 0 || pix == 0)
        return -1;
    if (ctx->slot_id >= 0 && ctx->slot_id != slot)
        gfx_slot_free(ctx->slot_id);
    ctx->slot_id = slot;
    ctx->pixels = pix;
    ctx->zbuf = zb;
    ctx->w = nw;
    ctx->h = nh;
    math3d_set_screen(nw, nh);
    gfx_zbuf_prepare(ctx, nw, nh);
    return 0;
}

int clvm_sys_dispatch(ClvmVm *vm, int32_t id, void *user) {
    int32_t a, b, c, d, e, f, g;
    const uint8_t *src;
    int32_t need;
    ClvmGfxCtx *ctx = gfx_ctx(vm, user);
    uint32_t *pix;
    int gw;
    int gh;

    if (vm == 0 || ctx == 0 || ctx->pixels == 0)
        return -1;
    pix = ctx->pixels;
    gw = ctx->w;
    gh = ctx->h;

    switch (id) {
    case 1:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_put(pix, gw, gh, a, b, c);
        return 0;
    case 2:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_fill(pix, gw, gh, a, b, c, d, e);
        return 0;
    case 3:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_line(pix, gw, gh, a, b, c, d, e);
        return 0;
    case 4:
        if (!pop_i32(vm, &f) || !pop_i32(vm, &e) || !pop_i32(vm, &d) ||
            !pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        if (d <= 0 || e <= 0)
            return -1;
        need = d * e;
        if (!vm_bytes(vm, a, need, &src))
            return -1;
        gfx2d_sprite(pix, gw, gh, src, b, c, d, e, f);
        return 0;
    case 5:
        if (!pop_i32(vm, &g) || !pop_i32(vm, &f) || !pop_i32(vm, &e) ||
            !pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a))
            return -1;
        if (b <= 0 || c <= 0 || d <= 0 || e <= 0)
            return -1;
        need = 16 * d * e + b * c;
        if (!vm_bytes(vm, a, need, &src))
            return -1;
        gfx2d_tilemap(pix, gw, gh, src, b, c, d, e, f, g);
        return 0;
    case 6:
        if (!pop_i32(vm, &a))
            return -1;
        gfx2d_clear(pix, gw, gh, a);
        return 0;
    case 10:
        if (!pop_i32(vm, &a))
            return -1;
        {
            extern int input_key_down(int scancode);
            if (!clvm_vm_push(vm, input_key_down(a)))
                return -1;
        }
        return 0;
    case 11:
        if (!clvm_vm_push(vm, (int32_t)now32()))
            return -1;
        return 0;
    case 12:
        if (!pop_i32(vm, &a) || a < 0)
            return -1;
        clvm_vm_wait(vm, now32() + (uint32_t)a);
        return 0;
    case 13:
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        if (a < 0 || b < 0)
            return -1;
        speaker_play((uint32_t)a, (uint32_t)b, now32());
        return 0;
    case 20: {
        int32_t x0;
        int32_t y0;
        int32_t z0;
        int32_t x1;
        int32_t y1;
        int32_t z1;
        int32_t x2;
        int32_t y2;
        int32_t z2;
        int32_t color;

        if (!pop_i32(vm, &color) || !pop_i32(vm, &z2) || !pop_i32(vm, &y2) ||
            !pop_i32(vm, &x2) || !pop_i32(vm, &z1) || !pop_i32(vm, &y1) ||
            !pop_i32(vm, &x1) || !pop_i32(vm, &z0) || !pop_i32(vm, &y0) ||
            !pop_i32(vm, &x0))
            return -1;
        gfx_zbuf_prepare(ctx, gw, gh);
        tri_fill(pix, gw, gh,
                 x0, y0, z0, x1, y1, z1, x2, y2, z2, color);
        return 0;
    }
    case 21:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx_zbuf_prepare(ctx, gw, gh);
        if (mesh_draw(vm, a, b, c, d, e, pix, gw, gh) != 0)
            return -1;
        return 0;
    case 23: {
        int32_t color;
        float yaw;
        float oz;
        float oy;
        float ox;
        if (!pop_i32(vm, &color) || !pop_f(vm, &yaw) || !pop_f(vm, &oz) ||
            !pop_f(vm, &oy) || !pop_f(vm, &ox) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx_zbuf_prepare(ctx, gw, gh);
        if (mesh_draw_f(vm, a, b, c, ox, oy, oz, yaw, color, pix, gw, gh) != 0)
            return -1;
        return 0;
    }
    case 33: {
        float pitch;
        float yaw;
        float z;
        float y;
        float x;
        if (!pop_f(vm, &pitch) || !pop_f(vm, &yaw) || !pop_f(vm, &z) ||
            !pop_f(vm, &y) || !pop_f(vm, &x))
            return -1;
        math3d_cam_set(x, y, z, yaw, pitch);
        return 0;
    }
    case 34: {
        float br;
        float bg;
        float bb;
        float z;
        float y;
        float x;
        if (!pop_f(vm, &bb) || !pop_f(vm, &bg) || !pop_f(vm, &br) ||
            !pop_f(vm, &z) || !pop_f(vm, &y) || !pop_f(vm, &x))
            return -1;
        shade_set_light(x, y, z, br, bg, bb);
        return 0;
    }
    case 35:
        if (!pop_i32(vm, &a))
            return -1;
        tex_set_slot(a);
        return 0;
    case 36:
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a))
            return -1;
        if (voxel_set(a, b, c, d) != 0)
            return -1;
        return 0;
    case 37:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        if (!clvm_vm_push(vm, voxel_get(a, b, c)))
            return -1;
        return 0;
    case 38:
        gfx_zbuf_prepare(ctx, gw, gh);
        if (voxel_world_draw(pix, gw, gh) != 0)
            return -1;
        return 0;
    case 39:
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        if (clvm_gfx_viewport(ctx, a, b) != 0)
            clvm_gfx_viewport(ctx, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H);
        return 0;
    case 40:
        if (!clvm_vm_push(vm, ctx->w))
            return -1;
        return 0;
    case 41:
        if (!clvm_vm_push(vm, ctx->h))
            return -1;
        return 0;
    case 31:
        if (!pop_i32(vm, &a))
            return -1;
        if (!clvm_vm_push(vm, (int32_t)gfx_sinf_bits((uint32_t)a)))
            return -1;
        return 0;
    case 32:
        if (!pop_i32(vm, &a))
            return -1;
        if (!clvm_vm_push(vm, (int32_t)gfx_cosf_bits((uint32_t)a)))
            return -1;
        return 0;
    case 22:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        if (mesh_transform(vm, a, b, c) != 0)
            return -1;
        return 0;
    case 30:
        if (!clvm_vm_push(vm, (int32_t)bench_fps_estimate()))
            return -1;
        return 0;
    default:
        return -1;
    }
}

void clvm_sys_blit_to(const uint32_t *src, int dx, int dy, int sw, int sh,
                      int max_w, int max_h) {
    int y;
    int dw;
    int dh;
    static int sxmap[1920];
    static int map_sw = -1;
    static int map_dw = -1;

    if (!src || g_gfx.back == 0 || g_gfx.width <= 0 || g_gfx.height <= 0)
        return;
    dw = max_w > 0 ? max_w : sw;
    dh = max_h > 0 ? max_h : sh;
    if (dx < 0) {
        dw += dx;
        dx = 0;
    }
    if (dy < 0) {
        dh += dy;
        dy = 0;
    }
    if (dx + dw > g_gfx.width)
        dw = g_gfx.width - dx;
    if (dy + dh > g_gfx.height)
        dh = g_gfx.height - dy;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    gfx_mark_dirty(dx, dy, dw, dh);
    if (sw == dw && sh == dh) {
        for (y = 0; y < dh; ++y) {
            uint32_t *dst = g_gfx.back + (dy + y) * g_gfx.width + dx;
            const uint32_t *row = src + (size_t)y * (size_t)sw;
            gfx_fast_copy_u32(dst, row, dw);
        }
        return;
    }
    if (dw > 1920)
        dw = 1920;
    if (map_sw != sw || map_dw != dw) {
        int x;
        for (x = 0; x < dw; ++x)
            sxmap[x] = x * sw / dw;
        map_sw = sw;
        map_dw = dw;
    }
    for (y = 0; y < dh; ++y) {
        uint32_t *dst = g_gfx.back + (dy + y) * g_gfx.width + dx;
        int sy = y * sh / dh;
        const uint32_t *row = src + (size_t)sy * (size_t)sw;
        int x;
        for (x = 0; x < dw; ++x)
            dst[x] = row[sxmap[x]];
    }
}

void clvm_sys_frame(uint32_t now) {
    speaker_poll(now);
}
