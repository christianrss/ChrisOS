#ifndef CHRIS_SHADE_H
#define CHRIS_SHADE_H

#include <stdint.h>
#include "math3d.h"

typedef struct ShadeState {
    Vec3f pos;
    Vec3f col;
} ShadeState;

void shade_set_light(float x, float y, float z, float r, float g, float b);
void shade_state_save(ShadeState *out);
void shade_state_load(const ShadeState *in);
void shade_get_light(Vec3f *pos, Vec3f *col);
uint32_t shade_phong(uint32_t rgb, float nx, float ny, float nz);

#endif
