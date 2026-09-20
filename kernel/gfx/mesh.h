#ifndef CHRIS_MESH_H
#define CHRIS_MESH_H

#include <stdint.h>
#include "clvm_vm.h"

int mesh_draw(ClvmVm *vm, int32_t addr, int32_t vertices, int32_t triangles,
              int32_t angle_deg, int32_t color, uint32_t *pixels, int w, int h);

int mesh_draw_f(ClvmVm *vm, int32_t addr, int32_t vertices, int32_t triangles,
                float ox, float oy, float oz, float yaw, int32_t color,
                uint32_t *pixels, int w, int h);

int mesh_transform(ClvmVm *vm, int32_t addr, int32_t mat_addr, int32_t n_verts);

#endif
