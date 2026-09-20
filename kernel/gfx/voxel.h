#ifndef CHRIS_VOXEL_H
#define CHRIS_VOXEL_H

#include <stdint.h>
#include "clvm_vm.h"

int voxel_set(int x, int y, int z, int id);
int voxel_get(int x, int y, int z);
int voxel_world_draw(uint32_t *pixels, int w, int h);
int voxel_mesh_rebuilds(void);

#endif
