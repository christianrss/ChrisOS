#include "clvm_sys.h"
#include "gfx2d.h"
#include "graphics.h"
#include "input.h"
#include "speaker.h"

extern volatile uint64_t ticks;

static uint32_t g_game[CLVM_SYS_GAME_W * CLVM_SYS_GAME_H];
static int g_game_ox = 40;
static int g_game_oy = 48;

static int pop_i32(ClvmVm *vm, int32_t *out) {
    if (!clvm_vm_pop(vm, out))
        return 0;
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
    return (uint32_t)ticks;
}

int clvm_sys_dispatch(ClvmVm *vm, int32_t id, void *user) {
    int32_t a, b, c, d, e, f, g;
    const uint8_t *src;
    int32_t need;
    (void)user;

    if (vm == 0)
        return -1;

    switch (id) {
    case 1:
        if (!pop_i32(vm, &c) || !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_put(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, a, b, c);
        return 0;
    case 2:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_fill(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, a, b, c, d, e);
        return 0;
    case 3:
        if (!pop_i32(vm, &e) || !pop_i32(vm, &d) || !pop_i32(vm, &c) ||
            !pop_i32(vm, &b) || !pop_i32(vm, &a))
            return -1;
        gfx2d_line(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, a, b, c, d, e);
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
        gfx2d_sprite(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, src, b, c, d, e, f);
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
        gfx2d_tilemap(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, src,
                      b, c, d, e, f, g);
        return 0;
    case 6:
        if (!pop_i32(vm, &a))
            return -1;
        gfx2d_clear(g_game, CLVM_SYS_GAME_W, CLVM_SYS_GAME_H, a);
        return 0;
    case 10:
        if (!pop_i32(vm, &a))
            return -1;
        if (!clvm_vm_push(vm, input_key_down(a)))
            return -1;
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
    default:
        return -1;
    }
}

void clvm_sys_blit(void) {
    int x;
    int y;
    int dw;
    int dh;

    if (g_gfx.back == 0 || g_gfx.width <= 0 || g_gfx.height <= 0)
        return;
    dw = CLVM_SYS_GAME_W;
    dh = CLVM_SYS_GAME_H;
    if (g_game_ox < 0)
        g_game_ox = 0;
    if (g_game_oy < 0)
        g_game_oy = 0;
    if (g_game_ox + dw > g_gfx.width)
        dw = g_gfx.width - g_game_ox;
    if (g_game_oy + dh > g_gfx.height)
        dh = g_gfx.height - g_game_oy;
    if (dw <= 0 || dh <= 0)
        return;
    for (y = 0; y < dh; ++y) {
        uint32_t *dst = g_gfx.back + (g_game_oy + y) * g_gfx.width + g_game_ox;
        uint32_t *src = g_game + y * CLVM_SYS_GAME_W;
        for (x = 0; x < dw; ++x)
            dst[x] = src[x];
    }
}

void clvm_sys_frame(uint32_t now) {
    speaker_poll(now);
    clvm_sys_blit();
}
