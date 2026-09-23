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

/* On-disk GPT type GUID for a ChrisFS partition (mixed-endian). */
int gpt_find_cfs(BlockDevice *disk, uint32_t *lba, uint32_t *count);

#endif
