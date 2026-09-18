/* LEARN:STOR64-S01 */
#ifndef CHRIS_BLOCK_DEVICE_H
#define CHRIS_BLOCK_DEVICE_H

#include <stdint.h>

enum {
    BD_OK = 0,
    BD_EINVAL = -1,
    BD_ERANGE = -2,
    BD_ETIMEOUT = -3,
    BD_EIO = -4,
    BD_ENODEV = -5,
    BD_EROFS = -6
};

typedef int (*BlockReadFn)(void *ctx, uint32_t lba,
                           uint32_t count, void *dst);
typedef int (*BlockWriteFn)(void *ctx, uint32_t lba,
                            uint32_t count, const void *src);
typedef int (*BlockFlushFn)(void *ctx);

typedef struct BlockDevice {
    void *ctx;
    uint32_t sector_size;
    uint32_t sector_count;
    BlockReadFn read;
    BlockWriteFn write;
    BlockFlushFn flush;
    uint8_t writable;
} BlockDevice;

static inline int bd_range_ok(const BlockDevice *d, uint32_t lba,
                              uint32_t count) {
    if (!d || !count || d->sector_size != 512u)
        return 0;
    if (lba >= d->sector_count)
        return 0;
    return count <= d->sector_count - lba;
}

static inline int bd_read(BlockDevice *d, uint32_t lba,
                          uint32_t count, void *dst) {
    if (!dst || !d || !d->read)
        return BD_EINVAL;
    if (!bd_range_ok(d, lba, count))
        return BD_ERANGE;
    return d->read(d->ctx, lba, count, dst);
}

static inline int bd_write(BlockDevice *d, uint32_t lba,
                           uint32_t count, const void *src) {
    if (!src || !d || !d->write)
        return BD_EINVAL;
    if (!d->writable)
        return BD_EROFS;
    if (!bd_range_ok(d, lba, count))
        return BD_ERANGE;
    return d->write(d->ctx, lba, count, src);
}

static inline int bd_flush(BlockDevice *d) {
    if (!d)
        return BD_EINVAL;
    return d->flush ? d->flush(d->ctx) : BD_OK;
}

#endif
