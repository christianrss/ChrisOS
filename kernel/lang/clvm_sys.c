/* LEARN:WS64-W08 */
#include "clvm_sys.h"
#include <stdint.h>
#include "bench.h"
#include "cfs.h"
#include "fs.h"
#include "cla/cla.h"
#include "gc/gc.h"
#include "gfx2d.h"
#include "gfx_fast.h"
#include "gfx_slot.h"
#include "graphics.h"
#include "heap.h"
#include "math3d.h"
#include "mesh.h"
#include "phys.h"
#include "scene.h"
#include "pmm.h"
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
#include "cls/cls.h"
#include "serial.h"
#include "proc.h"
#include "sock.h"
#include "sha256.h"
#include "aes.h"
#include "x25519.h"
#include "rng.h"
#include "ac97.h"
#include "hwgate.h"
#include "port.h"
#include "pci.h"

#define CLVM_FD_MAX 32
#define CLVM_FD_PER_SLOT 8
#define CLVM_FD_CAP 65536u
#define CLVM_FD_MAX_BYTES (16u * 1024u * 1024u)
#define CLVM_TH_MAX 32
#define CLVM_TH_PER 8
#define CLVM_PAL_SLOTS 16

typedef struct ClvmTh {
    int used;
    int done;
    int blocked;
    int slot;
    int proc;
    int mtx_addr;
    int cnd_addr;
    ClvmVm *parent;
    ClvmVm *vm;
} ClvmTh;

static ClvmTh g_th[CLVM_TH_MAX];
static uint8_t g_cla_tmp[65536];

typedef struct ClvmFile {
    int used;
    int slot;
    int dirty;
    int streaming;
    uint32_t size;
    uint32_t pos;
    uint32_t cap;
    char path[FS_PATH];
    uint8_t *buf;
} ClvmFile;

static ClvmFile g_fds[CLVM_FD_MAX];
static uint8_t g_pal[CLVM_PAL_SLOTS][768];
static int g_pal_set[CLVM_PAL_SLOTS];

static int th_count_slot(int slot) {
    int i;
    int n = 0;
    for (i = 0; i < CLVM_TH_MAX; ++i) {
        if (g_th[i].used && g_th[i].slot == slot)
            n++;
    }
    return n;
}

static void th_wake_mutex(int addr) {
    int i;
    for (i = 0; i < CLVM_TH_MAX; ++i) {
        if (g_th[i].used && g_th[i].blocked && g_th[i].mtx_addr == addr) {
            g_th[i].blocked = 0;
            g_th[i].mtx_addr = 0;
            if (g_th[i].vm)
                clvm_vm_wake(g_th[i].vm, 0);
            return;
        }
    }
}

static void th_wake_cond(int addr) {
    int i;
    for (i = 0; i < CLVM_TH_MAX; ++i) {
        if (g_th[i].used && g_th[i].blocked && g_th[i].cnd_addr == addr) {
            g_th[i].blocked = 0;
            if (g_th[i].vm)
                clvm_vm_wake(g_th[i].vm, 0);
            return;
        }
    }
}

void clvm_threads_tick(void) {
    int i;
    for (i = 0; i < CLVM_TH_MAX; ++i) {
        ClvmStepResult r;
        ClvmTh *t = &g_th[i];
        if (!t->used || t->done || t->blocked || !t->vm)
            continue;
        if (t->proc > 0)
            proc_switch(t->proc);
        r = clvm_step(t->vm, 8192u);
        proc_switch(PROC_KERNEL);
        if (r == CLVM_STEP_HALT || r == CLVM_STEP_FAULT) {
            t->done = 1;
            if (t->parent && t->parent->join_wait == i)
                clvm_vm_wake(t->parent, 0);
            if (t->proc > 0)
                proc_unblock(t->proc);
        }
    }
}

static int guest_load_i32(ClvmVm *vm, int64_t addr, int32_t *out) {
    uint32_t v;
    if (!vm->memory || addr < 0 || (uint64_t)addr + 4u > vm->mem_size)
        return 0;
    v = (uint32_t)vm->memory[addr] |
        ((uint32_t)vm->memory[addr + 1] << 8) |
        ((uint32_t)vm->memory[addr + 2] << 16) |
        ((uint32_t)vm->memory[addr + 3] << 24);
    *out = (int32_t)v;
    return 1;
}

static int guest_store_i32(ClvmVm *vm, int64_t addr, int32_t val) {
    uint32_t v = (uint32_t)val;
    if (!vm->memory || addr < 0 || (uint64_t)addr + 4u > vm->mem_size)
        return 0;
    vm->memory[addr] = (uint8_t)v;
    vm->memory[addr + 1] = (uint8_t)(v >> 8);
    vm->memory[addr + 2] = (uint8_t)(v >> 16);
    vm->memory[addr + 3] = (uint8_t)(v >> 24);
    return 1;
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

static int guest_read(ClvmVm *vm, uint64_t off, uint8_t *dst, int n) {
    int i;
    if (!vm || !vm->memory || !dst || n < 0)
        return 0;
    if (off + (uint64_t)n > vm->mem_size)
        return 0;
    for (i = 0; i < n; ++i)
        dst[i] = vm->memory[off + (uint64_t)i];
    return 1;
}

static int guest_write(ClvmVm *vm, uint64_t off, const uint8_t *src, int n) {
    int i;
    if (!vm || !vm->memory || !src || n < 0)
        return 0;
    if (off + (uint64_t)n > vm->mem_size)
        return 0;
    for (i = 0; i < n; ++i)
        vm->memory[off + (uint64_t)i] = src[i];
    return 1;
}

static int drv_allowed(ClvmVm *vm) {
    int slot;
    const char *name;
    int i;
    slot = lang_find_slot_by_gfx(vm ? vm->sys_user : 0);
    name = lang_slot_name(slot);
    if (!name)
        return 0;
    for (i = 0; name[i]; ++i) {
        if (name[i] == 'D' && name[i + 1] == 'R' && name[i + 2] == 'V')
            return 1;
    }
    return 0;
}

#define SYS_TRACE 32
static struct {
    int id;
    int slot;
} g_sys_trace[SYS_TRACE];
static int g_sys_n;

void clvm_sys_trace(int index, int *id, int *slot) {
    int n;
    int i;
    if (id)
        *id = 0;
    if (slot)
        *slot = -1;
    if (index < 0 || index >= SYS_TRACE || g_sys_n <= 0)
        return;
    n = g_sys_n < SYS_TRACE ? g_sys_n : SYS_TRACE;
    if (index >= n)
        return;
    i = (g_sys_n - 1 - index) % SYS_TRACE;
    if (i < 0)
        i += SYS_TRACE;
    if (id)
        *id = g_sys_trace[i].id;
    if (slot)
        *slot = g_sys_trace[i].slot;
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

/* Strip leading "./" segments Doom's IWAD search adds. */
static void path_normalize(char *p) {
    int i;
    while (p[0] == '.' && p[1] == '/' ) {
        i = 0;
        do {
            p[i] = p[i + 2];
            i++;
        } while (p[i - 1]);
    }
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

    if (!vm_cstr(vm, path_addr, path, FS_PATH)) {
        serial_puts("fopen: bad path addr\n");
        return clvm_vm_push(vm, -1) ? 0 : -1;
    }
    path_normalize(path);
    serial_puts("fopen: ");
    serial_puts(path);
    serial_puts("\n");
    if (!path_ok(path)) {
        serial_puts("fopen: path_ok fail\n");
        return clvm_vm_push(vm, -1) ? 0 : -1;
    }
    if (fd_count_slot(slot) >= CLVM_FD_PER_SLOT) {
        serial_puts("fopen: too many fds\n");
        return clvm_vm_push(vm, -1) ? 0 : -1;
    }
    for (i = 3; i < CLVM_FD_MAX && g_fds[i].used; ++i) {
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
    /* Missing files: fail (do not open an empty ghost fd). */
    if (fs_stat(path, &sz, &ty) != 0 || ty != CFS_INODE_FILE) {
        serial_puts("fopen: missing\n");
        return clvm_vm_push(vm, -1) ? 0 : -1;
    }
    g_fds[i].streaming = sz > CLVM_FD_CAP;
    g_fds[i].buf = 0;
    if (g_fds[i].streaming) {
        cap = 0;
        n = (int)sz;
    } else {
        cap = sz > 0 ? sz : CLVM_FD_CAP;
        if (cap > CLVM_FD_MAX_BYTES)
            cap = CLVM_FD_MAX_BYTES;
        g_fds[i].buf = (uint8_t *)kmalloc(cap);
        if (!g_fds[i].buf)
            return clvm_vm_push(vm, -1) ? 0 : -1;
        n = fs_read(path, g_fds[i].buf, (int)cap);
        if (n < 0) {
            kfree(g_fds[i].buf);
            g_fds[i].buf = 0;
            return clvm_vm_push(vm, -1) ? 0 : -1;
        }
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
    serial_puts("fopen ok size=");
    serial_write_u64((uint64_t)(uint32_t)n);
    serial_puts("\n");
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
    g_fds[i].streaming = 0;
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
    if (take) {
        if (g_fds[fd].streaming) {
            int got;
            if (addr < 0 ||
                (uint64_t)(uint32_t)addr + take > vm->mem_size)
                return clvm_vm_push(vm, -1) ? 0 : -1;
            got = fs_read_at(g_fds[fd].path, g_fds[fd].pos,
                             vm->memory + (uint32_t)addr, (int)take);
            if (got < 0)
                return clvm_vm_push(vm, -1) ? 0 : -1;
            take = (uint32_t)got;
        } else if (!vm_copy_out(vm, addr, (int32_t)take,
                                g_fds[fd].buf + g_fds[fd].pos)) {
            return clvm_vm_push(vm, -1) ? 0 : -1;
        }
    }
    g_fds[fd].pos += take;
    return clvm_vm_push(vm, (int32_t)take) ? 0 : -1;
}

static int sys_fwrite(ClvmVm *vm, int32_t fd, int32_t addr, int32_t n) {
    uint32_t end;
    /* Stdout/stderr style fds used by putchar/printf: mirror to serial. */
    if ((fd == 1 || fd == 2) && n > 0) {
        char tmp[128];
        int32_t left = n;
        int32_t off = 0;
        while (left > 0) {
            int32_t take = left < 128 ? left : 128;
            int32_t i;
            if (!vm_copy_in(vm, addr + off, take, (uint8_t *)tmp))
                return clvm_vm_push(vm, -1) ? 0 : -1;
            /* Raw bytes — do not stop at embedded NUL (serial_puts would). */
            for (i = 0; i < take; i++)
                serial_putc(tmp[i]);
            off += take;
            left -= take;
        }
        return clvm_vm_push(vm, n) ? 0 : -1;
    }
    if (fd < 0 || fd >= CLVM_FD_MAX || !g_fds[fd].used || n < 0)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    if (g_fds[fd].streaming)
        return clvm_vm_push(vm, -1) ? 0 : -1;
    end = g_fds[fd].pos + (uint32_t)n;
    if (end > g_fds[fd].cap) {
        uint32_t ncap = g_fds[fd].cap;
        uint8_t *nb;
        uint32_t i;
        if (ncap < 4096u)
            ncap = 4096u;
        while (ncap < end) {
            if (ncap >= 1024u * 1024u)
                return clvm_vm_push(vm, -1) ? 0 : -1;
            ncap *= 2u;
        }
        nb = (uint8_t *)kmalloc(ncap);
        if (!nb)
            return clvm_vm_push(vm, -1) ? 0 : -1;
        for (i = 0; i < g_fds[fd].size && g_fds[fd].buf; ++i)
            nb[i] = g_fds[fd].buf[i];
        if (g_fds[fd].buf)
            kfree(g_fds[fd].buf);
        g_fds[fd].buf = nb;
        g_fds[fd].cap = ncap;
    }
    if (n && !vm_copy_in(vm, addr, n, g_fds[fd].buf + g_fds[fd].pos))
        return clvm_vm_push(vm, -1) ? 0 : -1;
    {
        uint32_t start = g_fds[fd].pos;
        g_fds[fd].pos += (uint32_t)n;
        if (start == 0)
            g_fds[fd].size = g_fds[fd].pos;
        else if (g_fds[fd].pos > g_fds[fd].size)
            g_fds[fd].size = g_fds[fd].pos;
    }
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
    if (*w < 64)
        *w = 64;
    if (*h < 24)
        *h = 24;
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
    int ui2d;

    if (ctx == 0)
        return -1;
    if (w <= 0 || h <= 0)
        clvm_gfx_native_size(&nw, &nh);
    else {
        nw = w;
        nh = h;
        clamp_view(&nw, &nh);
    }
    /*
     * Large UI / wallpaper: 2D slot (no z-buffer, halves VRAM).
     * Game-sized viewports keep a private z-buffer for mesh/voxel.
     */
    ui2d = (ctx->zbuf == 0);
    if (ctx->slot_id >= 0 && ctx->w == nw && ctx->h == nh && ctx->pixels != 0 &&
        (ui2d ? ctx->zbuf == 0 : ctx->zbuf != 0)) {
        math3d_set_screen(nw, nh);
        gfx_zbuf_prepare(ctx, nw, nh);
        return 0;
    }
    pix = 0;
    zb = 0;
    slot = -1;
    if (ui2d) {
        if (ctx->slot_id >= 0)
            slot = gfx_slot_resize2d(ctx->slot_id, nw, nh, &pix);
        if (slot < 0)
            slot = gfx_slot_alloc2d(nw, nh, &pix);
        zb = 0;
    } else {
        if (ctx->slot_id >= 0)
            slot = gfx_slot_resize(ctx->slot_id, nw, nh, &pix, &zb);
        if (slot < 0)
            slot = gfx_slot_alloc(nw, nh, &pix, &zb);
    }
    if (slot < 0 && (nw != CLVM_SYS_GAME_W || nh != CLVM_SYS_GAME_H)) {
        nw = CLVM_SYS_GAME_W;
        nh = CLVM_SYS_GAME_H;
        ui2d = 0;
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
    int py;
    int row_w;
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
    row_w = x1 - x0;
    if (row_w <= 0) {
        return;
    }
    for (py = y0; py < y1; ++py) {
        gfx_fast_fill_u32(pix + py * gw + x0, row_w, rgb);
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
        if (bits == 0) {
            continue;
        }
        for (col = 0; col < fw; ++col) {
            uint32_t mask = 1u << (unsigned int)(fw - col - 1);
            if ((bits & mask) != 0) {
                slot_put(pix, gw, gh, x + col, y + row, rgb);
            }
        }
    }
}

static void slot_text_n(uint32_t *pix, int gw, int gh, int x, int y,
                        const uint8_t *s, int n, uint32_t rgb) {
    int pen;
    int adv;
    int row_y;
    int i;
    if (!s || n <= 0) {
        return;
    }
    pen = x;
    row_y = y;
    adv = gfx_text_advance(font_arial_width);
    for (i = 0; i < n; i++) {
        unsigned char ch = s[i];
        if (ch == '\n') {
            pen = x;
            row_y += font_arial_height;
            continue;
        }
        if (ch == 0) {
            break;
        }
        slot_glyph(pix, gw, gh, pen, row_y, ch, rgb);
        pen += adv;
    }
}

static int guest_len(ClvmVm *vm, int32_t addr, int cap) {
    uint64_t a;
    int n;
    if (!vm || !vm->memory || addr < 0 || cap < 1) {
        return 0;
    }
    a = (uint64_t)(uint32_t)addr;
    if (a >= vm->mem_size) {
        return -1;
    }
    n = 0;
    while (n < cap && a + (uint64_t)n < vm->mem_size) {
        if (vm->memory[a + (uint64_t)n] == 0) {
            return n;
        }
        n++;
    }
    return n;
}

static int32_t guest_i32(ClvmVm *vm, int32_t addr) {
    const uint8_t *p;
    if (!vm_bytes(vm, addr, 4, &p)) {
        return 0;
    }
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
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
    {
        int tr = g_sys_n % SYS_TRACE;
        g_sys_trace[tr].id = id;
        g_sys_trace[tr].slot = lang_find_slot_by_gfx(ctx);
        g_sys_n++;
    }

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
        /* Bind this slot's z-buffer before clear — otherwise clear hits the
         * previous slot's bind (or static) and 3D draws into a dirty private zbuf. */
        gfx_zbuf_prepare(ctx, gw, gh);
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
        lang_slot_publish(lang_slot_of(ctx));
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
        (void)mesh_draw(vm, a, b, c, d, e, pix, gw, gh);
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
        (void)mesh_draw_f(vm, a, b, c, ox, oy, oz, yaw, color, pix, gw, gh);
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
        (void)sys_fclose(a);
        return clvm_vm_push(vm, 0) ? 0 : -1;
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
    if (!pop_i32(vm, &a) || !vm_cstr(vm, a, path, FS_PATH))
            return clvm_vm_push(vm, -1) ? 0 : -1;
        path_normalize(path);
        if (!path_ok(path))
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
        int slot;
        ClvmVm *child;
        if (!clvm_vm_pop64(vm, &arg) || !clvm_vm_pop64(vm, &fn))
            return -1;
        slot = lang_slot_of((ClvmGfxCtx *)vm->sys_user);
        if (th_count_slot(slot) >= CLVM_TH_PER)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
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
        child->join_wait = -1;
        (void)clvm_vm_push64(child, arg);
        g_th[i].used = 1;
        g_th[i].done = 0;
        g_th[i].blocked = 0;
        g_th[i].slot = slot;
        g_th[i].mtx_addr = 0;
        g_th[i].cnd_addr = 0;
        g_th[i].proc = proc_current();
        g_th[i].parent = vm;
        g_th[i].vm = child;
        return clvm_vm_push64(vm, i) ? 0 : -1;
    }
    case 63: {
        int64_t a;
        if (!clvm_vm_pop64(vm, &a))
            return -1;
        if (a < 0 || a >= CLVM_TH_MAX || !g_th[a].used)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (!g_th[a].done) {
            vm->join_wait = (int32_t)a;
            vm->state = CLVM_WAITING;
            proc_block(proc_current(), PROC_ST_BLOCK_JOIN);
            if (!clvm_vm_push64(vm, a) || !clvm_vm_push64(vm, 63))
                return -1;
            return 0;
        }
        vm->join_wait = -1;
        if (g_th[a].vm)
            kfree(g_th[a].vm);
        g_th[a].vm = 0;
        g_th[a].used = 0;
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 127: {
        int64_t addr;
        int32_t word;
        int self;
        if (!clvm_vm_pop64(vm, &addr))
            return -1;
        if (!guest_load_i32(vm, addr, &word))
            return -1;
        self = 1;
        {
            int i;
            for (i = 0; i < CLVM_TH_MAX; ++i) {
                if (g_th[i].used && g_th[i].vm == vm) {
                    self = i + 2;
                    break;
                }
            }
        }
        if (word == 0 || word == self) {
            if (!guest_store_i32(vm, addr, self))
                return -1;
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        }
        {
            int i;
            for (i = 0; i < CLVM_TH_MAX; ++i) {
                if (g_th[i].vm == vm) {
                    g_th[i].blocked = 1;
                    g_th[i].mtx_addr = (int)addr;
                    break;
                }
            }
        }
        vm->state = CLVM_WAITING;
        if (!clvm_vm_push64(vm, addr) || !clvm_vm_push64(vm, 127))
            return -1;
        return 0;
    }
    case 128: {
        int64_t addr;
        if (!clvm_vm_pop64(vm, &addr))
            return -1;
        if (!guest_store_i32(vm, addr, 0))
            return -1;
        th_wake_mutex((int)addr);
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 129: {
        int64_t mtx;
        int64_t cnd;
        if (!clvm_vm_pop64(vm, &mtx) || !clvm_vm_pop64(vm, &cnd))
            return -1;
        if (!guest_store_i32(vm, mtx, 0))
            return -1;
        th_wake_mutex((int)mtx);
        {
            int i;
            int already = 0;
            for (i = 0; i < CLVM_TH_MAX; ++i) {
                if (g_th[i].vm == vm && g_th[i].cnd_addr == (int)cnd &&
                    !g_th[i].blocked) {
                    already = 1;
                    g_th[i].cnd_addr = 0;
                    break;
                }
            }
            if (already) {
                if (!clvm_vm_push64(vm, mtx) || !clvm_vm_push64(vm, 127))
                    return -1;
                return 0;
            }
            for (i = 0; i < CLVM_TH_MAX; ++i) {
                if (g_th[i].vm == vm) {
                    g_th[i].blocked = 1;
                    g_th[i].cnd_addr = (int)cnd;
                    g_th[i].mtx_addr = (int)mtx;
                    break;
                }
            }
        }
        vm->state = CLVM_WAITING;
        if (!clvm_vm_push64(vm, cnd) || !clvm_vm_push64(vm, mtx) ||
            !clvm_vm_push64(vm, 129))
            return -1;
        return 0;
    }
    case 130: {
        int64_t cnd;
        if (!clvm_vm_pop64(vm, &cnd))
            return -1;
        th_wake_cond((int)cnd);
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 131: {
        int64_t idx;
        if (!clvm_vm_pop64(vm, &idx))
            return -1;
        if (idx < 0 || idx >= 16)
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, vm->tls[idx]) ? 0 : -1;
    }
    case 132: {
        int64_t idx;
        int64_t val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &idx))
            return -1;
        if (idx >= 0 && idx < 16)
            vm->tls[idx] = val;
        return 0;
    }
    case 140: {
        int64_t port;
        if (!clvm_vm_pop64(vm, &port))
            return -1;
        return clvm_vm_push64(vm, sock_listen((uint16_t)port)) ? 0 : -1;
    }
    case 141: {
        int64_t fd;
        if (!clvm_vm_pop64(vm, &fd))
            return -1;
        return clvm_vm_push64(vm, sock_accept((int)fd)) ? 0 : -1;
    }
    case 142: {
        int64_t ip;
        int64_t port;
        if (!clvm_vm_pop64(vm, &port) || !clvm_vm_pop64(vm, &ip))
            return -1;
        return clvm_vm_push64(vm, sock_connect((uint32_t)ip, (uint16_t)port)) ? 0 : -1;
    }
    case 143: {
        int64_t fd, addr, n;
        uint8_t tmp[200];
        int i;
        int rc;
        if (!clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &addr) || !clvm_vm_pop64(vm, &fd))
            return -1;
        if (n > 200)
            n = 200;
        if (n < 0 || !vm->memory || (uint64_t)addr + (uint64_t)n > vm->mem_size)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        for (i = 0; i < (int)n; ++i)
            tmp[i] = vm->memory[addr + i];
        rc = sock_send((int)fd, tmp, (int)n);
        return clvm_vm_push64(vm, rc) ? 0 : -1;
    }
    case 144: {
        int64_t fd, addr, n;
        uint8_t tmp[200];
        int rc;
        int i;
        if (!clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &addr) || !clvm_vm_pop64(vm, &fd))
            return -1;
        if (n > 200)
            n = 200;
        if (n < 0 || !vm->memory || (uint64_t)addr + (uint64_t)n > vm->mem_size)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        sock_bind_proc((int)fd, proc_current());
        rc = sock_recv((int)fd, tmp, (int)n);
        if (rc == 0) {
            vm->state = CLVM_WAITING;
            proc_block(proc_current(), PROC_ST_BLOCK_SOCK);
            if (!clvm_vm_push64(vm, fd) || !clvm_vm_push64(vm, addr) ||
                !clvm_vm_push64(vm, n) || !clvm_vm_push64(vm, 144))
                return -1;
            return 0;
        }
        if (rc > 0) {
            for (i = 0; i < rc; ++i)
                vm->memory[addr + i] = tmp[i];
        }
        return clvm_vm_push64(vm, rc) ? 0 : -1;
    }
    case 145: {
        int64_t fd;
        if (!clvm_vm_pop64(vm, &fd))
            return -1;
        return clvm_vm_push64(vm, sock_close((int)fd)) ? 0 : -1;
    }
    case 146: {
        int64_t addr;
        char name[64];
        uint32_t ip = 0;
        int rc;
        if (!clvm_vm_pop64(vm, &addr))
            return -1;
        if (!guest_cstr(vm, (uint64_t)addr, name, (int)sizeof(name)))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        rc = sock_dns(name, &ip);
        (void)rc;
        return clvm_vm_push64(vm, (int64_t)ip) ? 0 : -1;
    }
    case 133: {
        int64_t addr;
        int64_t n;
        int slot;
        if (!clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &addr))
            return -1;
        slot = lang_find_slot_by_gfx(ctx);
        if (n < 0)
            n = 0;
        if (n > 256)
            n = 256;
        if (!vm->memory || (uint64_t)addr + (uint64_t)n > vm->mem_size)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, lang_checkpoint_save(slot, vm->memory + addr,
                                                       (int)n, (uint32_t)addr))
                   ? 0
                   : -1;
    }
    case 134: {
        int slot = lang_find_slot_by_gfx(ctx);
        return clvm_vm_push64(vm, lang_hot_reload(lang_slot_name(slot))) ? 0 : -1;
    }
    case 150: {
        return clvm_vm_push64(vm, (int64_t)rng_u32()) ? 0 : -1;
    }
    case 151: {
        int64_t src, n, dst;
        uint8_t tmp[256];
        uint8_t dig[32];
        if (!clvm_vm_pop64(vm, &dst) || !clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &src))
            return -1;
        if (n < 0 || n > 256)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (!guest_read(vm, (uint64_t)src, tmp, (int)n))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        sha256(tmp, (unsigned)n, dig);
        if (!guest_write(vm, (uint64_t)dst, dig, 32))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, 32) ? 0 : -1;
    }
    case 152: {
        int64_t key, in, out;
        uint8_t k[16], b[16], o[16];
        if (!clvm_vm_pop64(vm, &out) || !clvm_vm_pop64(vm, &in) || !clvm_vm_pop64(vm, &key))
            return -1;
        if (!guest_read(vm, (uint64_t)key, k, 16) || !guest_read(vm, (uint64_t)in, b, 16))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        aes128_encrypt(k, b, o);
        if (!guest_write(vm, (uint64_t)out, o, 16))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, 16) ? 0 : -1;
    }
    case 153: {
        int64_t out, scalar, point;
        uint8_t s[32], p[32], o[32];
        if (!clvm_vm_pop64(vm, &point) || !clvm_vm_pop64(vm, &scalar) ||
            !clvm_vm_pop64(vm, &out))
            return -1;
        if (!guest_read(vm, (uint64_t)scalar, s, 32) || !guest_read(vm, (uint64_t)point, p, 32))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        x25519(o, s, p);
        if (!guest_write(vm, (uint64_t)out, o, 32))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, 32) ? 0 : -1;
    }
    case 160: {
        int64_t addr, n;
        int16_t tmp[128];
        int rc;
        if (!clvm_vm_pop64(vm, &n) || !clvm_vm_pop64(vm, &addr))
            return -1;
        if (n > 128)
            n = 128;
        if (n < 0 || !guest_read(vm, (uint64_t)addr, (uint8_t *)tmp, (int)n * 2))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        rc = ac97_write(tmp, (int)n);
        return clvm_vm_push64(vm, rc) ? 0 : -1;
    }
    case 170: {
        int64_t port, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &port))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        outw((uint16_t)port, (uint16_t)val);
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 171: {
        int64_t port;
        if (!clvm_vm_pop64(vm, &port))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, inw((uint16_t)port)) ? 0 : -1;
    }
    case 172: {
        int64_t port, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &port))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        outb((uint16_t)port, (uint8_t)val);
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 173: {
        int64_t port;
        if (!clvm_vm_pop64(vm, &port))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, inb((uint16_t)port)) ? 0 : -1;
    }
    case 174: {
        int64_t irq;
        if (!clvm_vm_pop64(vm, &irq))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (!ac97_take_event((int)irq)) {
            vm->state = CLVM_WAITING;
            proc_block(proc_current(), PROC_ST_BLOCK_IRQ);
            if (!clvm_vm_push64(vm, irq) || !clvm_vm_push64(vm, 174))
                return -1;
            return 0;
        }
        return clvm_vm_push64(vm, 1) ? 0 : -1;
    }
    case 175: {
        int64_t bus, dev, fn, off;
        if (!clvm_vm_pop64(vm, &off) || !clvm_vm_pop64(vm, &fn) ||
            !clvm_vm_pop64(vm, &dev) || !clvm_vm_pop64(vm, &bus))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                           (uint8_t)off))
                   ? 0
                   : -1;
    }
    case 210: {
        int64_t bus, dev, fn, off, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &off) ||
            !clvm_vm_pop64(vm, &fn) || !clvm_vm_pop64(vm, &dev) ||
            !clvm_vm_pop64(vm, &bus))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_pci_write((int)bus, (int)dev, (int)fn,
                                               (int)off, (uint32_t)val))
                   ? 0
                   : -1;
    }
    case 211: {
        int64_t bus, dev, fn, bar;
        if (!clvm_vm_pop64(vm, &bar) || !clvm_vm_pop64(vm, &fn) ||
            !clvm_vm_pop64(vm, &dev) || !clvm_vm_pop64(vm, &bus))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_bar_map((int)bus, (int)dev, (int)fn,
                                             (int)bar))
                   ? 0
                   : -1;
    }
    case 212: {
        int64_t win, off;
        if (!clvm_vm_pop64(vm, &off) || !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_r32((int)win, (uint32_t)off)) ? 0 : -1;
    }
    case 213: {
        int64_t win, off, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &off) ||
            !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_w32((int)win, (uint32_t)off,
                                              (uint32_t)val))
                   ? 0
                   : -1;
    }
    case 214: {
        int64_t win, off;
        if (!clvm_vm_pop64(vm, &off) || !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_r8((int)win, (uint32_t)off)) ? 0 : -1;
    }
    case 215: {
        int64_t win, off, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &off) ||
            !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_w8((int)win, (uint32_t)off,
                                             (uint32_t)val))
                   ? 0
                   : -1;
    }
    case 216: {
        int64_t win, off;
        if (!clvm_vm_pop64(vm, &off) || !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_r16((int)win, (uint32_t)off)) ? 0 : -1;
    }
    case 217: {
        int64_t win, off, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &off) ||
            !clvm_vm_pop64(vm, &win))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_mmio_w16((int)win, (uint32_t)off,
                                              (uint32_t)val))
                   ? 0
                   : -1;
    }
    case 218: {
        int64_t pages;
        if (!clvm_vm_pop64(vm, &pages))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_dma_alloc((int)pages)) ? 0 : -1;
    }
    case 219: {
        int64_t id;
        if (!clvm_vm_pop64(vm, &id))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_dma_lo((int)id)) ? 0 : -1;
    }
    case 220: {
        int64_t id;
        if (!clvm_vm_pop64(vm, &id))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_dma_hi((int)id)) ? 0 : -1;
    }
    case 221: {
        int64_t id, off, val;
        if (!clvm_vm_pop64(vm, &val) || !clvm_vm_pop64(vm, &off) ||
            !clvm_vm_pop64(vm, &id))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_dma_w32((int)id, (uint32_t)off,
                                             (uint32_t)val))
                   ? 0
                   : -1;
    }
    case 222: {
        int64_t id, off;
        if (!clvm_vm_pop64(vm, &off) || !clvm_vm_pop64(vm, &id))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_dma_r32((int)id, (uint32_t)off)) ? 0 : -1;
    }
    case 223:
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, 0) ? 0 : -1;
        return clvm_vm_push64(vm, hw_disk_sectors()) ? 0 : -1;
    case 224: {
        int64_t lba, ptr, nsec;
        uint8_t tmp[4096];
        if (!clvm_vm_pop64(vm, &nsec) || !clvm_vm_pop64(vm, &ptr) ||
            !clvm_vm_pop64(vm, &lba))
            return -1;
        if (!drv_allowed(vm) || nsec < 1 || nsec > 8)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (hw_disk_read((uint32_t)lba, tmp, (int)nsec) != 0)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (!guest_write(vm, (uint64_t)ptr, tmp, (int)nsec * 512))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    case 225: {
        int64_t lba, ptr, nsec;
        uint8_t tmp[4096];
        if (!clvm_vm_pop64(vm, &nsec) || !clvm_vm_pop64(vm, &ptr) ||
            !clvm_vm_pop64(vm, &lba))
            return -1;
        if (!drv_allowed(vm) || nsec < 1 || nsec > 8)
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        if (!guest_read(vm, (uint64_t)ptr, tmp, (int)nsec * 512))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_disk_write((uint32_t)lba, tmp, (int)nsec))
                   ? 0
                   : -1;
    }
    case 226:
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_disk_format()) ? 0 : -1;
    case 227: {
        int64_t q, cmd, fb, w, h, nwin, noff;
        if (!clvm_vm_pop64(vm, &noff) || !clvm_vm_pop64(vm, &nwin) ||
            !clvm_vm_pop64(vm, &h) || !clvm_vm_pop64(vm, &w) ||
            !clvm_vm_pop64(vm, &fb) || !clvm_vm_pop64(vm, &cmd) ||
            !clvm_vm_pop64(vm, &q))
            return -1;
        if (!drv_allowed(vm))
            return clvm_vm_push64(vm, -1) ? 0 : -1;
        return clvm_vm_push64(vm, hw_gpu_arm((int)q, (int)cmd, (int)fb, (int)w,
                                             (int)h, (int)nwin, (uint32_t)noff))
                   ? 0
                   : -1;
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
        Task *t = task_of_ctx(ctx);
        int top;
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
        top = task_id_at(m.x, m.y);
        if (t && top >= 0 && top != t->id) {
            btn = 0;
        } else if (t && gw > 0 && gh > 0 &&
                   gw <= CLVM_SYS_GAME_W + 32 && gh <= CLVM_SYS_GAME_H + 32) {
            int cy = t->frame.y + TASK_TITLE_HEIGHT;
            int cb = t->frame.y + t->frame.body_height;
            if (m.y < cy || m.y >= cb || m.x < t->frame.x ||
                m.x >= t->frame.x + t->frame.width) {
                btn = 0;
            }
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
        const uint8_t *src8;
        int slen;
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a)) {
            return -1;
        }
        slen = guest_len(vm, c, 2048);
        if (slen < 0) {
            return -1;
        }
        if (slen > 0) {
            if (!vm_bytes(vm, c, slen, &src8)) {
                return -1;
            }
            slot_text_n(pix, gw, gh, a, b, src8, slen, (uint32_t)d);
        }
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
        if (b < 0)
            b = 0;
        task_move(t->id, a, b);
        if (c > 0 && d > 0)
            task_resize(t->id, c, d);
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
        if (b < 0)
            b = 0;
        task_move(t->id, a, b);
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
    case 113: {
        Task *t;
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a) || a <= 0 || b <= 0)
            return -1;
        if (clvm_gfx_viewport(ctx, a, b) != 0)
            return -1;
        t = task_of_ctx(ctx);
        if (t)
            task_resize(t->id, a, b);
        return 0;
    }
    case 114: {
        Task *t = task_of_ctx(ctx);
        if (t)
            task_minimize(t->id);
        return 0;
    }
    case 115: {
        Task *t;
        TaskRect bounds;
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a) || c <= 0 || d <= 0)
            return -1;
        if (clvm_gfx_viewport(ctx, c, d) != 0)
            return -1;
        t = task_of_ctx(ctx);
        if (t) {
            bounds.x = a;
            bounds.y = b < 0 ? 0 : b;
            bounds.width = c;
            bounds.body_height = d;
            task_maximize(t->id, bounds);
        }
        return 0;
    }
    case 116: {
        Task *t;
        if (!pop_i32(vm, &d) || !pop_i32(vm, &c) || !pop_i32(vm, &b) ||
            !pop_i32(vm, &a) || c <= 0 || d <= 0)
            return -1;
        if (clvm_gfx_viewport(ctx, c, d) != 0)
            return -1;
        t = task_of_ctx(ctx);
        if (t) {
            task_restore(t->id);
            task_move(t->id, a, b < 0 ? 0 : b);
            task_resize(t->id, c, d);
        }
        return 0;
    }
    case 117:
        return clvm_vm_push(vm, (int32_t)(heap_used_bytes() / 1024u)) ? 0 : -1;
    case 118:
        return clvm_vm_push(vm, (int32_t)(heap_free_bytes() / 1024u)) ? 0 : -1;
    case 119:
        return clvm_vm_push(vm, (int32_t)pmm_free_pages()) ? 0 : -1;
    case 120:
        return clvm_vm_push(vm, (int32_t)bench_frame_p50_ms()) ? 0 : -1;
    case 121:
        return clvm_vm_push(vm, (int32_t)bench_frame_p95_ms()) ? 0 : -1;
    case 122: {
        Cfs *fs = storage_cfs();
        return clvm_vm_push(vm, (int32_t)cfs_cache_hits(fs)) ? 0 : -1;
    }
    case 123: {
        Cfs *fs = storage_cfs();
        return clvm_vm_push(vm, (int32_t)cfs_cache_misses(fs)) ? 0 : -1;
    }
    case 124:
        return clvm_vm_push(vm, lang_active_count()) ? 0 : -1;
    case 125: {
        Task *t;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        t = task_iter(a);
        if (!t) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        task_raise(t->id);
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 126: {
        int32_t rec;
        int32_t nrec;
        int32_t i;
        int32_t rx;
        int32_t ry;
        int32_t rgb;
        int32_t rlen;
        int32_t rstr;
        const uint8_t *src8;
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        if (b < 0) {
            return -1;
        }
        if (b > 256) {
            b = 256;
        }
        rec = a;
        nrec = b;
        for (i = 0; i < nrec; i++) {
            rx = guest_i32(vm, rec);
            ry = guest_i32(vm, rec + 4);
            rgb = guest_i32(vm, rec + 8);
            rlen = guest_i32(vm, rec + 12);
            rstr = guest_i32(vm, rec + 16);
            if (rlen < 0) {
                rlen = 0;
            }
            if (rlen > 256) {
                rlen = 256;
            }
            if (rlen > 0 && rstr >= 0) {
                if (vm_bytes(vm, rstr, rlen, &src8)) {
                    slot_text_n(pix, gw, gh, rx, ry, src8, rlen, (uint32_t)rgb);
                }
            }
            rec += 20;
        }
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
        int ok;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        serial_puts("app_spawn ");
        serial_puts(path);
        serial_puts("\n");
        ok = lang_run_path(path) ? 1 : 0;
        if (!ok) {
            serial_puts("app_spawn failed\n");
        }
        return clvm_vm_push(vm, ok) ? 0 : -1;
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
        /* Editor Go: replace any previous run of this CLV. */
        return clvm_vm_push(vm, lang_run_path_replace(path) ? 1 : 0) ? 0 : -1;
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
    case 105: {
        char msg[160];
        const char *le;
        int n = 0;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        le = lang_last_error();
        if (!le) {
            le = "";
        }
        while (le[n] && n + 1 < (int)sizeof(msg)) {
            msg[n] = le[n];
            n++;
        }
        msg[n] = 0;
        n++;
        if (!vm_copy_out(vm, a, n, (const uint8_t *)msg)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 106: {
        char path[FS_PATH];
        char arg[FS_PATH];
        if (!pop_i32(vm, &b) || !pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH) ||
            !guest_cstr(vm, (uint64_t)(uint32_t)b, arg, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, lang_run_path_arg(path, arg) ? 1 : 0) ? 0 : -1;
    }
    case 107: {
        char arg[FS_PATH];
        int n;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!lang_copy_app_arg(arg, (int)sizeof(arg))) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        n = 0;
        while (arg[n]) {
            n++;
        }
        n++;
        if (!vm_copy_out(vm, a, n, (const uint8_t *)arg)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, 1) ? 0 : -1;
    }
    case 108: {
        char path[FS_PATH];
        char err[120];
        int id;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, -1) ? 0 : -1;
        }
        err[0] = 0;
        id = cls_runtime_load(path, err, (int)sizeof(err));
        if (id < 0 && err[0]) {
            serial_puts(err);
            serial_puts("\n");
        }
        return clvm_vm_push(vm, id) ? 0 : -1;
    }
    case 109: {
        char path[FS_PATH];
        char err[120];
        int id;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, -1) ? 0 : -1;
        }
        err[0] = 0;
        id = cls_runtime_reload(path, err, (int)sizeof(err));
        if (id < 0 && err[0]) {
            serial_puts(err);
            serial_puts("\n");
        }
        return clvm_vm_push(vm, id) ? 0 : -1;
    }
    case 110: {
        char path[FS_PATH];
        uint32_t sz;
        uint16_t ty;
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (!guest_cstr(vm, (uint64_t)(uint32_t)a, path, FS_PATH)) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        if (fs_stat(path, &sz, &ty) != 0) {
            return clvm_vm_push(vm, 0) ? 0 : -1;
        }
        return clvm_vm_push(vm, ty == CFS_INODE_DIR ? 1 : 0) ? 0 : -1;
    }
    case 111: {
        if (!pop_i32(vm, &a)) {
            return -1;
        }
        if (a == 1) {
            input_set_layout(INPUT_LAYOUT_ABNT2);
        } else {
            input_set_layout(INPUT_LAYOUT_US);
        }
        (void)input_save_layout_file("SYS/KB.CFG");
        return clvm_vm_push(vm, (int32_t)input_get_layout()) ? 0 : -1;
    }
    case 112:
        return clvm_vm_push(vm, (int32_t)input_get_layout()) ? 0 : -1;
    case 180: {
        int64_t inc;
        if (!clvm_vm_pop64(vm, &inc))
            return -1;
        if (inc < 0)
            inc = 0;
        return clvm_vm_push64(vm, (int64_t)proc_sbrk(proc_current(),
                                                     (uint64_t)inc))
                   ? 0
                   : -1;
    }
    case 190: {
        int64_t mesh, x, y, z, yaw, color;
        if (!clvm_vm_pop64(vm, &color) || !clvm_vm_pop64(vm, &yaw) ||
            !clvm_vm_pop64(vm, &z) || !clvm_vm_pop64(vm, &y) ||
            !clvm_vm_pop64(vm, &x) || !clvm_vm_pop64(vm, &mesh))
            return -1;
        return clvm_vm_push64(vm, scene_add((int)mesh, (int)x, (int)y, (int)z,
                                            (int)yaw, (int)color))
                   ? 0
                   : -1;
    }
    case 191: {
        int64_t camx, camz, yaw;
        if (!clvm_vm_pop64(vm, &yaw) || !clvm_vm_pop64(vm, &camz) ||
            !clvm_vm_pop64(vm, &camx))
            return -1;
        if (ctx && ctx->pixels)
            scene_draw(ctx->pixels, ctx->w, ctx->h, (int)camx, (int)camz,
                       (int)yaw);
        return clvm_vm_push64(vm, scene_visible((int)camx, (int)camz, (int)yaw))
                   ? 0
                   : -1;
    }
    case 192: {
        int64_t x, y, w, h;
        if (!clvm_vm_pop64(vm, &h) || !clvm_vm_pop64(vm, &w) ||
            !clvm_vm_pop64(vm, &y) || !clvm_vm_pop64(vm, &x))
            return -1;
        return clvm_vm_push64(vm, phys_add((int)x, (int)y, (int)w, (int)h))
                   ? 0
                   : -1;
    }
    case 193:
        phys_step();
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    case 194: {
        int64_t t, x, y, z, yaw;
        if (!clvm_vm_pop64(vm, &yaw) || !clvm_vm_pop64(vm, &z) ||
            !clvm_vm_pop64(vm, &y) || !clvm_vm_pop64(vm, &x) ||
            !clvm_vm_pop64(vm, &t))
            return -1;
        return clvm_vm_push64(vm, anim_key((int)t, (int)x, (int)y, (int)z,
                                           (int)yaw))
                   ? 0
                   : -1;
    }
    case 195: {
        int64_t node, t;
        if (!clvm_vm_pop64(vm, &t) || !clvm_vm_pop64(vm, &node))
            return -1;
        anim_apply((int)node, (int)t);
        return clvm_vm_push64(vm, 0) ? 0 : -1;
    }
    default:
        return -1;
    }
}

void clvm_sys_blit_to(const uint32_t *src, int dx, int dy, int sw, int sh,
                      int max_w, int max_h) {
    int y;
    int dw;
    int dh;

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
    /* Never scale: 1:1 copy of the top-left overlapping region only. */
    if (dw > sw)
        dw = sw;
    if (dh > sh)
        dh = sh;
    if (dw <= 0 || dh <= 0)
        return;
    for (y = 0; y < dh; ++y) {
        uint32_t *dst = g_gfx.back + (dy + y) * g_gfx.width + dx;
        const uint32_t *row = src + (size_t)y * (size_t)sw;
        gfx_fast_copy_u32(dst, row, dw);
    }
}

void clvm_sys_blit_scaled(const uint32_t *src, int sw, int sh, int dx, int dy,
                          int dw, int dh) {
    gfx_blit_scaled(src, sw, sh, dx, dy, dw, dh);
}

void clvm_sys_frame(uint32_t now) {
    speaker_poll(now);
}
