#ifndef CHRIS_CLVM_SYS_H
#define CHRIS_CLVM_SYS_H

#include <stdint.h>
#include "../compiler/clvm/clvm_vm.h"

#define CLVM_SYS_GAME_W 320
#define CLVM_SYS_GAME_H 200
#define CLVM_SYS_GAME_MAX_W 1920
#define CLVM_SYS_GAME_MAX_H 1080

typedef struct ClvmGfxCtx {
    uint32_t *pixels;
    uint32_t *zbuf;
    int w;
    int h;
    int slot_id;
} ClvmGfxCtx;

void clvm_gfx_native_size(int *w, int *h);
int clvm_gfx_viewport(ClvmGfxCtx *ctx, int w, int h);

int clvm_sys_dispatch(ClvmVm *vm, int32_t id, void *user);
void clvm_sys_frame(uint32_t now);
void clvm_threads_tick(void);
void clvm_sys_trace(int index, int *id, int *slot);
void clvm_sys_blit_to(const uint32_t *src, int dx, int dy, int sw, int sh,
                      int max_w, int max_h);
void clvm_sys_blit_scaled(const uint32_t *src, int sw, int sh, int dx, int dy,
                          int dw, int dh);
void clvm_sys_close_slot(int slot_id);

#endif
