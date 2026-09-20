#ifndef CHRIS_TEX_H
#define CHRIS_TEX_H

#include <stdint.h>

#define TEX_SIZE 16
#define TEX_SLOTS 16

void tex_init(void);
void tex_set_slot(int slot);
int tex_slot(void);
uint32_t tex_sample(int slot, float u, float v);

#endif
