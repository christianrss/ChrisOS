#include "part.h"

static int part_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    PartView *p = (PartView *)ctx;
    return p->parent->read(p->parent->ctx, p->start + lba, count, dst);
}

static int part_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    PartView *p = (PartView *)ctx;
    return p->parent->write(p->parent->ctx, p->start + lba, count, src);
}

int part_open(PartView *out, BlockDevice *parent, uint32_t start,
              uint32_t count) {
    if (!out || !parent || !count || start >= parent->sector_count)
        return -1;
    if (count > parent->sector_count - start)
        count = parent->sector_count - start;
    out->parent = parent;
    out->start = start;
    out->count = count;
    out->dev.ctx = out;
    out->dev.sector_size = 512;
    out->dev.sector_count = count;
    out->dev.read = part_read;
    out->dev.write = part_write;
    out->dev.flush = 0;
    out->dev.writable = parent->writable;
    return 0;
}
