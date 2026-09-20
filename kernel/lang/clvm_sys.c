/* LEARN:WS64-W08 */
#include "clvm_sys.h"
#include <stdint.h>
#include "bench.h"
#include "cfs.h"
#include "fs.h"
#include "cla/cla.h"
#include "gc/gc.h"
#include "kthread.h"
#include "gfx2d.h"
#include "gfx_fast.h"
#include "gfx_slot.h"
#include "graphics.h"
#include "heap.h"
#include "math3d.h"
#include "mesh.h"
#include "shade.h"
#include "speaker.h"
#include "storage.h"
#include "task.h"
#include "tex.h"
#include "tri.h"
#include "ui.h"
#include "voxel.h"
#include "zbuf.h"
#include "font.h"
#include "input.h"
#include "lang_pipeline.h"
#include "chrismake.h"

#define CLVM_FD_MAX 32
#define CLVM_FD_PER_SLOT 8
#define CLVM_FD_CAP 65536u
#define CLVM_FD_MAX_BYTES (16u * 1024u * 1024u)
#define CLVM_TH_MAX 4
#define CLVM_PAL_SLOTS 16

typedef struct ClvmTh {
    int used;
    int done;
    int kid;
    ClvmVm *vm;
} ClvmTh;

static ClvmTh g_th[CLVM_TH_MAX];
static uint8_t g_cla_tmp[65536];

typedef struct ClvmFile {
    int used;
    int slot;
    int dirty;
    uint32_t size;
    uint32_t pos;
    uint32_t cap;
    char path[FS_PATH];
    uint8_t *buf;
} ClvmFile;

static ClvmFile g_fds[CLVM_FD_MAX];
static uint8_t g_pal[CLVM_PAL_SLOTS][768];
static int g_pal_set[CLVM_PAL_SLOTS];

static void clvm_th_run(void *arg) {
    ClvmTh *t = (ClvmTh *)arg;
    if (!t || !t->vm)
        return;
    t->vm->state = CLVM_READY;
    for (;;) {
        ClvmStepResult r = clvm_step(t->vm, 4096);
        if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT)
            break;
        if (r == CLVM_STEP_YIELD)
            clvm_vm_wake(t->vm, 0);
    }
    t->done = 1;
}

static int guest_cstr(ClvmVm *vm, uint64_t off, char *out, int cap) {
    int i = 0;
    if (!vm || !vm->memory || !out || cap < 2 || off >= vm->mem_size)
        return 0;
    while (i + 1 < cap && off + (uint64_t)i < vm->mem_size) {
        char ch = (char)vm->memory[off + (uint64_t)i];
        out[i] = ch;
        if (ch == 0)
            return 1;
        i++;
    }
    out[i] = 0;
    return 1;
}

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
    uint64_t a;
    uint64_t n;
    if (addr < 0 || length <= 0 || vm->memory == 0)
        return 0;
    a = (uint64_t)(uint32_t)addr;
    n = (uint64_t)(uint32_t)length;
    if (a >= vm->mem_size)
        return 0;
    if (n > vm->mem_size - a)
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

static int fd_slot(void *user) {
    ClvmGfxCtx *ctx = gfx_ctx(0, user);
    if (ctx == 0)
        return 0;
    return ctx->slot_id < 0 ? 0 : ctx->slot_id;
}

static int path_ok(const char *p) {
    int i;
    if (!p || !p[0])
        return 0;
    for (i = 0; p[i]; ++i) {
        if (p[i] == '.' && p[i + 1] == '.' &&
            (i == 0 || p[i - 1] == '/') &&
            (p[i + 2] == 0 || p[i + 2] == '/'))
            return 0;
    }
    return 1;
}

static int vm_cstr(ClvmVm *vm, int32_t addr, char *out, int cap) {
    int i = 0;
    if (!vm || !out || cap < 2 || addr < 0)
        return 0;
    while (i + 1 < cap) {
        if ((uint64_t)(uint32_t)addr + (uint32_t)i >= vm->mem_size)
            return 0;
        out[i] = (char)vm->memory[(uint32_t)addr + (uint32_t)i];
        if (out[i] == 0)
            return 1;
        ++i;
    }
    return 0;
}

static int vm_copy_in(ClvmVm *vm, int32_t addr, int32_t n, uint8_t *dst) {
    uint32_t a;
    uint32_t i;
    if (addr < 0 || n < 0 || !dst)
        return 0;
    a = (uint32_t)addr;
    if (a >= vm->mem_size || (uint32_t)n > vm->mem_size - a)
        return 0;
    for (i = 0; i < (uint32_t)n; ++i)
        dst[i] = vm->memory[a + i];
    return 1;
}

static int vm_copy_out(ClvmVm *vm, int32_t addr, int32_t n, const uint8_t *src) {
    uint32_t a;
    uint32_t i;
    if (addr < 0 || n < 0 || !src)
        return 0;
    a = (uint32_t)addr;
    if (a >= vm->mem_size || (uint32_t)n > vm->mem_size - a)
        return 0;
    for (i = 0; i < (uint32_t)n; ++i)
        vm->memory[a + i] = src[i];
    return 1;
}

static int fd_count_slot(int slot) {
    int i;
    int n = 0;
    for (i = 0; i < CLVM_FD_MAX; ++i) {
        if (g_fds[i].used && g_fds[i].slot == slot)
            ++n;
    }
    return n;
}

static int sys_fopen(ClvmVm *vm, void *user, int32_t path_addr) {
    char path[FS_PATH];
    int slot = fd_slot(user);
    int i;
    int n;
    uint32_t sz = 0;
    uint16_t ty = 0;
    uint32_t cap;

    if (!vm_cstr(vm, path_addr, path, FS_PATH) || !path_ok(path))
        return clvm_vm_push(vm, -1) ? 0 : -1;
    if (fd_count_slot(slot) >= CLVM_FD_PER_SLOT)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    for (i = 0; i < CLVM_FD_MAX && g_fds[i].used; ++i) {
    }
    if (i == CLVM_FD_MAX)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    if (storage_ready() && storage_cfs()) {
        if (fs_stat(path, &sz, &ty) == 0 && ty == CFS_INODE_FILE) {
            if (cfs_perm(storage_cfs(), path, CFS_PERM_READ) != CFS_OK &&
                cfs_perm(storage_cfs(), path, CFS_PERM_WRITE) != CFS_OK)
                return clvm_vm_push(vm, -1) ? 0 : -1;
        }
    }
    cap = CLVM_FD_CAP;
    if (fs_stat(path, &sz, &ty) == 0 && sz > 0) {
        cap = sz;
        if (cap > CLVM_FD_MAX_BYTES)
            cap = CLVM_FD_MAX_BYTES;
    }
    g_fds[i].buf = (uint8_t *)kmalloc(cap);
    if (!g_fds[i].buf)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    n = fs_read(path, g_fds[i].buf, (int)cap);
    if (n < 0) {
        n = 0;
    }
    {
        int k = 0;
        while (path[k] && k < FS_PATH - 1) {
            g_fds[i].path[k] = path[k];
            ++k;
        }
        g_fds[i].path[k] = 0;
    }
    g_fds[i].used = 1;
    g_fds[i].slot = slot;
    g_fds[i].dirty = 0;
    g_fds[i].size = (uint32_t)n;
    g_fds[i].pos = 0;
    g_fds[i].cap = cap;
    return clvm_vm_push(vm, i) ? 0 : -1;
}

static void fd_free(int i) {
    if (i < 0 || i >= CLVM_FD_MAX || !g_fds[i].used)
        return;
    if (g_fds[i].dirty && g_fds[i].buf)
        fs_write(g_fds[i].path, g_fds[i].buf, (int)g_fds[i].size);
    if (g_fds[i].buf)
        kfree(g_fds[i].buf);
    g_fds[i].buf = 0;
    g_fds[i].used = 0;
    g_fds[i].dirty = 0;
}

static int sys_fclose(int32_t fd) {
    if (fd < 0 || fd >= CLVM_FD_MAX || !g_fds[fd].used)
        return -1;
    fd_free(fd);
    return 0;
}

static int sys_fread(ClvmVm *vm, int32_t fd, int32_t addr, int32_t n) {
    uint32_t left;
    uint32_t take;
    if (fd < 0 || fd >= CLVM_FD_MAX || !g_fds[fd].used || n < 0)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    left = g_fds[fd].size - g_fds[fd].pos;
    take = (uint32_t)n < left ? (uint32_t)n : left;
    if (take && !vm_copy_out(vm, addr, (int32_t)take,
                             g_fds[fd].buf + g_fds[fd].pos))
        return clvm_vm_push(vm, -1) ? 0 : -1;
    g_fds[fd].pos += take;
    return clvm_vm_push(vm, (int32_t)take) ? 0 : -1;
}

static int sys_fwrite(ClvmVm *vm, int32_t fd, int32_t addr, int32_t n) {
    uint32_t end;
    if (fd < 0 || fd >= CLVM_FD_MAX || !g_fds[fd].used || n < 0)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    end = g_fds[fd].pos + (uint32_t)n;
    if (end > g_fds[fd].cap)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    if (n && !vm_copy_in(vm, addr, n, g_fds[fd].buf + g_fds[fd].pos))
        return clvm_vm_push(vm, -1) ? 0 : -1;
    g_fds[fd].pos += (uint32_t)n;
    if (g_fds[fd].pos > g_fds[fd].size)
        g_fds[fd].size = g_fds[fd].pos;
    g_fds[fd].dirty = 1;
    return clvm_vm_push(vm, n) ? 0 : -1;
}

static int sys_fseek(ClvmVm *vm, int32_t fd, int32_t off) {
    if (fd < 0 || fd >= CLVM_FD_MAX || !g_fds[fd].used)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    if (off < 0)
        off = 0;
    if ((uint32_t)off > g_fds[fd].size)
        off = (int32_t)g_fds[fd].size;
    g_fds[fd].pos = (uint32_t)off;
    return clvm_vm_push(vm, (int32_t)g_fds[fd].pos) ? 0 : -1;
}

void clvm_sys_close_slot(int slot_id) {
    int i;
    for (i = 0; i < CLVM_FD_MAX; ++i) {
        if (g_fds[i].used && g_fds[i].slot == slot_id)
            fd_free(i);
    }
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
    nh = g_gfx.height;
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

static int lang_slot_of(ClvmGfxCtx *ctx) {
    return lang_find_slot_by_gfx(ctx);
}

static Task *task_of_ctx(ClvmGfxCtx *ctx) {
    int slot;
    int tid;
    slot = lang_slot_of(ctx);
    if (slot < 0) {
        return 0;
    }
    tid = lang_slot_task(slot);
    return task_get(tid);
}

static void slot_put(uint32_t *pix, int gw, int gh, int x, int y, uint32_t rgb) {
    if (!pix || x < 0 || y < 0 || x >= gw || y >= gh) {
        return;
    }
    pix[y * gw + x] = rgb;
}

static void slot_fillrgb(uint32_t *pix, int gw, int gh, int x, int y, int rw,
                         int rh, uint32_t rgb) {
    int x0;
    int y0;
    int x1;
    int y1;
    int px;
    int py;
    if (!pix || rw <= 0 || rh <= 0) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + rw;
    y1 = y + rh;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > gw) {
        x1 = gw;
    }
    if (y1 > gh) {
        y1 = gh;
    }
    for (py = y0; py < y1; ++py) {
        for (px = x0; px < x1; ++px) {
            pix[py * gw + px] = rgb;
        }
    }
}

static void slot_glyph(uint32_t *pix, int gw, int gh, int x, int y,
                       unsigned int ch, uint32_t rgb) {
    int row;
    int col;
    int fw;
    int fh;
    fw = font_arial_width;
    fh = font_arial_height;
    for (row = 0; row < fh; ++row) {
        uint32_t bits = font_row(ch, row);
        for (col = 0; col < fw; ++col) {
            uint32_t mask = 1u << (unsigned int)(fw - col - 1);
            if ((bits & mask) != 0) {
                slot_put(pix, gw, gh, x + col, y + row, rgb);
            }
        }
    }
}

static void slot_text(uint32_t *pix, int gw, int gh, int x, int y, const char *s,
                      uint32_t rgb) {
    int pen;
    int adv;
    int row_y;
    if (!s) {
        return;
    }
    pen = x;
    row_y = y;
    adv = gfx_text_advance(font_arial_width);
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch == '\n') {
            pen = x;
            row_y += font_arial_height;
            continue;
        }
        slot_glyph(pix, gw, gh, pen, row_y, ch, rgb);
        pen += adv;
    }
}

typedef struct {
    int want;
    int cur;
    char *out;
    int cap;
    int found;
} ClvmReadDir;

static int readdir_cb(void *ctx, const char *name, uint32_t size, uint16_t type) {
    ClvmReadDir *r = (ClvmReadDir *)ctx;
    int i;
    (void)size;
    (void)type;
    if (!r || !name) {
        return 0;
    }
    if (r->cur == r->want) {
        i = 0;
        if (r->out && r->cap > 1) {
            while (name[i] && i + 1 < r->cap) {
                r->out[i] = name[i];
                i++;
            }
            r->out[i] = 0;
        }
        r->found = 1;
        return 1;
    }
    r->cur++;
    return 0;
}

static int sys_make_recipe(void *user, const char *recipe, char *err, int err_cap) {
    const char *p;
    int i;
    char path[FS_PATH];
    (void)user;
    if (!recipe) {
        return 0;
    }
    p = recipe;
    while (*p == ' ' || *p == '\t' || *p == '@') {
        p++;
    }
    if (p[0] == 'c' && p[1] == 'c' && (p[2] == ' ' || p[2] == '\t')) {
        p += 3;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        i = 0;
        while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' &&
               i + 1 < FS_PATH) {
            path[i] = p[i];
            i++;
        }
        path[i] = 0;
        if (!lang_compile_path(path)) {
            if (err && err_cap > 0) {
                const char *le = lang_last_error();
                i = 0;
                if (le) {
                    while (le[i] && i + 1 < err_cap) {
                        err[i] = le[i];
                        i++;
                    }
                }
                err[i] = 0;
            }
            return 0;
        }
        return 1;
    }
    if (p[0] == 'r' && p[1] == 'u' && p[2] == 'n' &&
        (p[3] == ' ' || p[3] == '\t')) {
        p += 4;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        i = 0;
        while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' &&
               i + 1 < FS_PATH) {
            path[i] = p[i];
            i++;
        }
        path[i] = 0;
        if (!lang_run_path(path)) {
            return 0;
        }
        return 1;
    }
    if (err && err_cap > 0) {
        err[0] = 0;
    }
    return 0;
}

static int push_ok(ClvmVm *vm, int ok) {
    return clvm_vm_push(vm, ok ? 0 : -1) ? 0 : -1;
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
    case 50:
        if (!pop_i32(vm, &a))
            return -1;
        return sys_fopen(vm, user, a);
    case 51:
        if (!pop_i32(vm, &a))
            return -1;
        return sys_fclose(a);
    case 52:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        return sys_fread(vm, a, b, c);
    case 53:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        return sys_fwrite(vm, a, b, c);
    case 54: {
        char path[FS_PATH];
        uint32_t sz = 0;
        uint16_t ty = 0;
        if (!pop_i32(vm, &a) || !vm_cstr(vm, a, path, FS_PATH) || !path_ok(path))
            return clvm_vm_push(vm, -1) ? 0 : -1;
        if (fs_stat(path, &sz, &ty) != 0)
            return clvm_vm_push(vm, -1) ? 0 : -1;
        return clvm_vm_push(vm, (int32_t)sz) ? 0 : -1;
    }
    case 55: {
        char path[FS_PATH];
        uint32_t sz = 0;
        uint16_t ty = 0;
        if (!pop_i32(vm, &a) || !vm_cstr(vm, a, path, FS_PATH) || !path_ok(path))
            return clvm_vm_push(vm, 0) ? 0 : -1;
        if (fs_stat(path, &sz, &ty) != 0)
            return clvm_vm_push(vm, 0) ? 0 : -1;
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 56: {
        int64_t n;
        uint64_t p = 0;
        if (!clvm_vm_pop64(vm, &n))
            return -1;
        if (!clvm_guest_malloc(vm, (uint64_t)n, &p))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, (int64_t)p) ? 0 : -1;
    }
    case 57: {
        int64_t p;
        if (!clvm_vm_pop64(vm, &p))
            return -1;
        clvm_guest_free(vm, (uint64_t)p);
        return 0;
    }
    case 58: {
        int64_t a;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (!clvm_guest_setjmp(vm, (uint64_t)a))
            return -1;
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 59: {
        int64_t a;
        int64_t v;
        if (!clvm_vm_pop64(vm, &v) || !clvm_vm_pop64(vm, &a))
            return -1;
        if (!clvm_guest_longjmp(vm, (uint64_t)a, v))
            return -1;
        return 0;
    }
    case 61: {
        int64_t p;
        int64_t n;
        uint64_t o = 0;
        if (!clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &p))
            return -1;
        if (!clvm_guest_realloc(vm, (uint64_t)p, (uint64_t)n, &o))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, (int64_t)o) ? 0 : -1;
    }
    case 70: {
        int64_t n;
        void *p;
        if (!clvm_vm_pop64(vm, &n))
            return -1;
        p = gc_alloc(1, (uint32_t)n);
        return clvm_vm_push64(vm, (int64_t)(uintptr_t)p) ? 0 : -1;
    }
    case 71:
        gc_collect();
        return 0;
    case 62: {
        int64_t fn;
        int64_t arg;
        int i;
        ClvmVm *child;
        if (!clvm_vm_pop64(vm, &arg) || !clvm_vm_pop64(vm, &fn))
            return -1;
        for (i = 0; i < CLVM_TH_MAX; ++i) {
            if (!g_th[i].used)
                break;
        }
        if (i == CLVM_TH_MAX)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        child = (ClvmVm *)kmalloc(sizeof(ClvmVm));
        if (!child)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        {
            uint32_t z;
            uint8_t *p = (uint8_t *)child;
            for (z = 0; z < sizeof(ClvmVm); ++z)
                p[z] = 0;
        }
        child->code = vm->code;
        child->code_size = vm->code_size;
        child->memory = vm->memory;
        child->mem_size = vm->mem_size;
        child->sys = vm->sys;
        child->sys_user = vm->sys_user;
        child->pc = (uint32_t)fn;
        child->state = CLVM_READY;
        (void)clvm_vm_push64(child, arg);
        g_th[i].used = 1;
        g_th[i].done = 0;
        g_th[i].vm = child;
        g_th[i].kid = kthread_create(clvm_th_run, &g_th[i]);
        return clvm_vm_push64(vm, i) ? 0 : -1;
    }
    case 63: {
        int64_t a;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (a >= 0 && a < CLVM_TH_MAX && g_th[a].used) {
            kthread_join(g_th[a].kid);
            if (g_th[a].vm)
                kfree(g_th[a].vm);
            g_th[a].vm = 0;
            g_th[a].used = 0;
        }
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 64: {
        int64_t a;
        char path[64];
        int n;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (!guest_cstr(vm, (uint64_t)a, path, (int)sizeof(path)))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        n = fs_read(path, g_cla_tmp, (int)sizeof(g_cla_tmp));
        if (n <= 0)
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, cla_load_bytes(g_cla_tmp, (size_t)n) ? 1 : 0)
                   ? 0
                   : -1;
    }
    case 65:
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        return sys_fseek(vm, a, b);
    case 72: {
        int32_t ptr;
        int32_t bw;
        int32_t bh;
        int x;
        int y;
        int slot;
        const uint8_t *src8;
        if (!pop_i32(vm, &bh) || !pop_i32(vm, &bw) || !pop_i32(vm, &ptr))
            return -1;
        if (bw <= 0 || bh <= 0 || bw > ctx->w || bh > ctx->h)
            return -1;
        if (!vm_bytes(vm, ptr, bw * bh, &src8))
            return -1;
        slot = ctx->slot_id >= 0 ? ctx->slot_id : 0;
        if (slot >= CLVM_PAL_SLOTS)
            slot = 0;
        for (y = 0; y < bh; ++y) {
            for (x = 0; x < bw; ++x) {
                uint8_t i8 = src8[y * bw + x];
                uint32_t col;
                if (g_pal_set[slot]) {
                    uint8_t *p = g_pal[slot] + (int)i8 * 3;
                    col = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) |
                          (uint32_t)p[2];
                } else {
                    col = ((uint32_t)i8 << 16) | ((uint32_t)i8 << 8) | i8;
                }
                pix[y * gw + x] = col | 0xff000000u;
            }
        }
        return 0;
    }
    case 73: {
        int32_t ptr;
        int slot;
        const uint8_t *src;
        int k;
        if (!pop_i32(vm, &ptr))
            return -1;
        if (!vm_bytes(vm, ptr, 768, &src))
            return -1;
        slot = ctx->slot_id >= 0 ? ctx->slot_id : 0;
        if (slot >= CLVM_PAL_SLOTS)
            slot = 0;
        for (k = 0; k < 768; ++k)
            g_pal[slot][k] = src[k];
        g_pal_set[slot] = 1;
        return 0;
    }
    case 80: {
        InputMouse m = input_mouse_snapshot();
        if (!clvm_vm_push(vm, m.x)) {
            return -1;
        }
        return 0;
    }
    case 81: {
        InputMouse m = input_mouse_snapshot();
        if (!clvm_vm_push(vm, m.y)) {
            return -1;
        }
        return 0;
    }
    case 82: {
        InputMouse m = input_mouse_snapshot();
        int32_t btn = 0;
        if (m.left_down) {
            btn |= 1;
        }
        if (m.right_down) {
            btn |= 2;
        }
        if (m.middle_down) {
            btn |= 4;
        }
        if (!clvm_vm_push(vm, btn)) {
            return -1;
        }
        return 0;
    }
    case 83: {
        int slot = lang_slot_of(ctx);
        if (!clvm_vm_push(vm, lang_slot_take_key(slot))) {
            return -1;
        }
        return 0;
    }
    case 84: {
        int slot = lang_slot_of(ctx);
        if (!clvm_vm_push(vm, lang_slot_take_text(slot))) {
            return -1;
        }
        return 0;
    }
    case 85:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        slot_fillrgb(pix, gw, gh, a, b, c, d, (uint32_t)e);
        return 0;
    case 86: {
        char msg[192];
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)c, msg, (int)sizeof(msg))) {
            return -1;
        }
        slot_text(pix, gw, gh, a, b, msg, (uint32_t)d);
        return 0;
    }
    case 87:
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a)) {
            return -1;
        }
        slot_glyph(pix, gw, gh, a, b, (unsigned int)c, (uint32_t)d);
        return 0;
    case 88: {
        Task *t;
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a)) {
            return -1;
        }
        t = task_of_ctx(ctx);
        if (!t) {
            return 0;
        }
        t->frame.x = a;
        t->frame.y = b;
        if (c > 0) {
            t->frame.width = c;
        }
        if (d > 0) {
            t->frame.body_height = d;
        }
        if (t->frame.y < 0) {
            t->frame.y = 0;
        }
        return 0;
    }
    case 89: {
        Task *t;
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        t = task_of_ctx(ctx);
        if (!t) {
            return 0;
        }
        t->frame.x = a;
        t->frame.y = b;
        if (t->frame.y < 0) {
            t->frame.y = 0;
        }
        return 0;
    }
    case 90: {
        Task *t = task_of_ctx(ctx);
        if (t) {
            if (!(t->frame.x <= 0 && t->frame.y <= 0 &&
                  t->frame.width >= g_gfx.width &&
                  t->frame.body_height >= g_gfx.height)) {
                task_raise(t->id);
            }
        }
        return 0;
    }
    case 91: {
        int slot = lang_slot_of(ctx);
        Task *t = task_of_ctx(ctx);
        if (t) {
            task_close(t->id);
        }
        lang_slot_request_close(slot);
        return 0;
    }
    case 92: {
        char path[FS_PATH];
        char name[64];
        ClvmReadDir rd;
        int rc;
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, -1) ? 0 : -1;
        }
        name[0] = 0;
        rd.want = b;
        rd.cur = 0;
        rd.out = name;
        rd.cap = (int)sizeof(name);
        rd.found = 0;
        rc = fs_list_at(path, readdir_cb, &rd);
        if (!rd.found) {
            (void)rc;
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        {
            int n = 0;
            while (name[n]) {
                n++;
            }
            n++;
            if (!vm_copy_out(vm, c, n, (const uint8_t *)name)) {
                return clvm_vm_push(vm, -1) ? 0 : -1;
            }
        }
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 93: {
        char path[FS_PATH];
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH) || !path_ok(path)) {
            return push_ok(vm, 0);
        }
        return push_ok(vm, fs_mkdir(path) == 0);
    }
    case 94: {
        char path[FS_PATH];
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH) || !path_ok(path)) {
            return push_ok(vm, 0);
        }
        return push_ok(vm, fs_unlink(path) == 0);
    }
    case 95: {
        char oldp[FS_PATH];
        char newp[FS_PATH];
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, oldp, FS_PATH) ||
            !guest_cstr(vm, (uint64_t)(uint32_t)b, newp, FS_PATH) ||
            !path_ok(oldp) || !path_ok(newp)) {
            return push_ok(vm, 0);
        }
        return push_ok(vm, fs_rename(oldp, newp) == 0);
    }
    case 96: {
        char path[FS_PATH];
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, lang_run_path(path) ? 1 : 0) ? 0 : -1;
    }
    case 97: {
        Task *t;
        int slot;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        t = task_iter(a);
        if (!t) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        if (t->type == TASK_APP) {
            slot = t->state.app.lang_slot;
            task_close(t->id);
            lang_slot_request_close(slot);
        } else {
            task_close(t->id);
        }
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 98:
        if (!clvm_vm_push(vm, task_count())) {
            return -1;
        }
        return 0;
    case 99: {
        Task *t;
        const char *title;
        int n;
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        t = task_iter(a);
        if (!t) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        title = task_title(t);
        n = 0;
        while (title[n]) {
            n++;
        }
        n++;
        if (!vm_copy_out(vm, b, n, (const uint8_t *)title)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 100: {
        char path[FS_PATH];
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, lang_compile_path(path) ? 1 : 0) ? 0 : -1;
    }
    case 101: {
        char path[FS_PATH];
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, lang_run_path(path) ? 1 : 0) ? 0 : -1;
    }
    case 102: {
        static char mktext[32768];
        static char mkerr[160];
        char path[FS_PATH];
        char target[64];
        int n;
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH) ||
            !guest_cstr(vm, (uint64_t)(uint32_t)b, target, (int)sizeof(target))) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        n = fs_read(path, mktext, (int)sizeof(mktext) - 1);
        if (n < 0) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        mktext[n] = 0;
        if (!target[0]) {
            target[0] = 'a';
            target[1] = 'l';
            target[2] = 'l';
            target[3] = 0;
        }
        return clvm_vm_push(vm, chrismake_run(mktext, target, sys_make_recipe, 0,
                                             mkerr, (int)sizeof(mkerr))
                                    ? 1
                                    : 0)
                   ? 0
                   : -1;
    }
    case 103:
        if (!clvm_vm_push(vm, g_gfx.width)) {
            return -1;
        }
        return 0;
    case 104:
        if (!clvm_vm_push(vm, g_gfx.height)) {
            return -1;
        }
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
