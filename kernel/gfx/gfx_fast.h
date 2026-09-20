#ifndef CHRIS_GFX_FAST_H
#define CHRIS_GFX_FAST_H

#include <stdint.h>

void gfx_fast_fill_u32(uint32_t *dst, int count, uint32_t value);
void gfx_fast_copy_u32(uint32_t *dst, const uint32_t *src, int count);

#endif
