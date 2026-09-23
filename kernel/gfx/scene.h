#ifndef CHRIS_SCENE_H
#define CHRIS_SCENE_H

#include <stdint.h>

#define SCENE_MAX 32

int scene_add(int mesh, int x, int y, int z, int yaw, int color);
void scene_clear(void);
int scene_count(void);
int scene_in_frustum(int camx, int camz, int yaw, int x, int z, int radius);
int scene_visible(int camx, int camz, int yaw);
void scene_draw(uint32_t *pixels, int w, int h, int camx, int camz, int yaw);
void anim_clear(void);
int anim_key(int t, int x, int y, int z, int yaw);
int anim_sample(int t, int *x, int *y, int *z, int *yaw);
void anim_apply(int node, int t);

#endif
