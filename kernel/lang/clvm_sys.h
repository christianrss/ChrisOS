#ifndef CHRIS_CLVM_SYS_H
#define CHRIS_CLVM_SYS_H

#include <stdint.h>
#include "../compiler/clvm/clvm_vm.h"

#define CLVM_SYS_GAME_W 320
#define CLVM_SYS_GAME_H 200

int clvm_sys_dispatch(ClvmVm *vm, int32_t id, void *user);
void clvm_sys_frame(uint32_t now);
void clvm_sys_blit_to(const uint32_t *src, int dx, int dy, int max_w, int max_h);

#endif