#ifndef CHRIS_TRI_BIN_H
#define CHRIS_TRI_BIN_H

#include <stdint.h>

#define TRI_BIN_MAX 4096

typedef struct TriBin {
    int screen_w;
    int screen_h;
    uint32_t entries[TRI_BIN_MAX];
    int count;
} TriBin;

void tri_bin_reset(TriBin *b, int screen_w, int screen_h);
void tri_bin_add(TriBin *b, int minx, int miny, int maxx, int maxy, uint16_t tri_id);

#endif
