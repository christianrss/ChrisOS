#ifndef CHRIS_GPURES_H
#define CHRIS_GPURES_H

#include <stdint.h>

/* 32 cached chunk meshes + window color/depth + atlas + scanout/cursor + headroom.
 * Chunk meshes share one DMA slab; this cap is VirtIO resource slots, not DMA slots.
 * Alloc failure is reported; callers evict or skip instead of panicking. */
#define GPU_RES_MAX 96
#define GPU_CTX_MAX 16

enum {
    GPU_RES_NONE = 0,
    GPU_RES_2D = 1,
    GPU_RES_3D = 2
};

enum {
    GPU_ST_FREE = 0,
    GPU_ST_ALLOC = 1,
    GPU_ST_CREATED = 2,
    GPU_ST_BACKED = 3,
    GPU_ST_ATTACHED = 4,
    GPU_ST_SCANOUT = 5
};

typedef struct GpuResource {
    uint32_t id;
    int owner;
    int type;
    int state;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t bind;
    uint32_t flags;
    int dma;
    uint32_t backing_off;
    uint32_t backing_size;
    int ctx_attached;
} GpuResource;

typedef struct GpuContext {
    uint32_t id;
    int owner;
    int live;
    uint32_t capset_id;
    uint64_t fence_next;
} GpuContext;

typedef struct GpuPool {
    GpuResource res[GPU_RES_MAX];
    GpuContext ctx[GPU_CTX_MAX];
    uint32_t next_res;
    uint32_t next_ctx;
    int res_live;
    int ctx_live;
} GpuPool;

void gpu_pool_init(GpuPool *p);
int gpu_res_alloc(GpuPool *p, int owner, uint32_t *id_out);
GpuResource *gpu_res_get(GpuPool *p, uint32_t id);
int gpu_res_release(GpuPool *p, int owner, uint32_t id);
int gpu_ctx_alloc(GpuPool *p, int owner, uint32_t capset, uint32_t *id_out);
GpuContext *gpu_ctx_get(GpuPool *p, uint32_t id);
int gpu_ctx_release(GpuPool *p, int owner, uint32_t id);
int gpu_res_live(const GpuPool *p);
int gpu_ctx_live(const GpuPool *p);

#endif
