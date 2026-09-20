/* PASSO 19 — tile binning (esqueleto) */
#include "tri_bin.h"

#define TILE_BITS 6
#define TILE_SIZE (1 << TILE_BITS)

void tri_bin_reset(TriBin *b, int screen_w, int screen_h) {
    b->screen_w = screen_w;
    b->screen_h = screen_h;
    b->count = 0;
}

void tri_bin_add(TriBin *b, int minx, int miny, int maxx, int maxy, uint16_t tri_id) {
    int tx0 = minx >> TILE_BITS;
    int ty0 = miny >> TILE_BITS;
    int tx1 = maxx >> TILE_BITS;
    int ty1 = maxy >> TILE_BITS;
    int ty;
    int tx;
    for (ty = ty0; ty <= ty1; ++ty) {
        for (tx = tx0; tx <= tx1; ++tx) {
            if (b->count < TRI_BIN_MAX)
                b->entries[b->count++] = (uint32_t)((tri_id << 16) | (ty << 8) | tx);
        }
    }
}
