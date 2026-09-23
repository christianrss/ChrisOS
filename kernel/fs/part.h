#ifndef CHRIS_PART_H
#define CHRIS_PART_H

#include "block_device.h"

typedef struct PartView {
    BlockDevice *parent;
    uint32_t start;
    uint32_t count;
    BlockDevice dev;
} PartView;

int part_open(PartView *out, BlockDevice *parent, uint32_t start,
              uint32_t count);

#endif
