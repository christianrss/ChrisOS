#ifndef CHRIS_TEX_H
#define CHRIS_TEX_H

#include <stdint.h>

#define TEX_SIZE 16
#define TEX_SLOTS 16

void tex_init(void);
typedef struct TexState {
    int slot;
    float du;
    float dv;
} TexState;

void tex_set_slot(int slot);
void tex_state_save(TexState *out);
void tex_state_load(const TexState *in);
void tex_ofs(float du, float dv);
int tex_slot(void);
uint32_t tex_sample(int slot, float u, float v);

#endif
