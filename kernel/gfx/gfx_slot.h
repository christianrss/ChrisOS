#ifndef CHRIS_GFX_SLOT_H
#define CHRIS_GFX_SLOT_H

#include <stdint.h>

int gfx_slot_alloc(int w, int h, uint32_t **pixels_out, uint32_t **zbuf_out);
int gfx_slot_alloc2d(int w, int h, uint32_t **pixels_out);
int gfx_slot_resize(int slot_id, int w, int h, uint32_t **pixels_out,
                    uint32_t **zbuf_out);
int gfx_slot_resize2d(int slot_id, int w, int h, uint32_t **pixels_out);
void gfx_slot_free(int slot_id);

#endif
