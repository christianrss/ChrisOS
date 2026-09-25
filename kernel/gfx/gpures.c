#include "gpures.h"

void gpu_pool_init(GpuPool *p) {
    uint32_t i;
    if (!p) {
        return;
    }
    for (i = 0; i < GPU_RES_MAX; ++i) {
        p->res[i].id = 0;
        p->res[i].owner = 0;
        p->res[i].type = GPU_RES_NONE;
        p->res[i].state = GPU_ST_FREE;
        p->res[i].dma = -1;
        p->res[i].ctx_attached = 0;
        p->res[i].width = 0;
        p->res[i].height = 0;
        p->res[i].depth = 0;
        p->res[i].format = 0;
        p->res[i].bind = 0;
        p->res[i].flags = 0;
        p->res[i].backing_size = 0;
    }
    for (i = 0; i < GPU_CTX_MAX; ++i) {
        p->ctx[i].id = 0;
        p->ctx[i].owner = 0;
        p->ctx[i].live = 0;
        p->ctx[i].capset_id = 0;
        p->ctx[i].fence_next = 1;
    }
    p->next_res = 1;
    p->next_ctx = 1;
    p->res_live = 0;
    p->ctx_live = 0;
}

static int id_live_res(const GpuPool *p, uint32_t id) {
    uint32_t i;
    for (i = 0; i < GPU_RES_MAX; ++i) {
        if (p->res[i].state != GPU_ST_FREE && p->res[i].id == id) {
            return 1;
        }
    }
    return 0;
}

int gpu_res_alloc(GpuPool *p, int owner, uint32_t *id_out) {
    uint32_t slot;
    uint32_t spins;
    if (!p || !id_out) {
        return -1;
    }
    slot = GPU_RES_MAX;
    for (spins = 0; spins < GPU_RES_MAX; ++spins) {
        if (p->res[spins].state == GPU_ST_FREE) {
            slot = spins;
            break;
        }
    }
    if (slot == GPU_RES_MAX) {
        return -1;
    }
    spins = 0;
    while (id_live_res(p, p->next_res) || p->next_res == 0u) {
        p->next_res++;
        if (p->next_res == 0u) {
            p->next_res = 1;
        }
        if (++spins > 100000u) {
            return -1;
        }
    }
    p->res[slot].id = p->next_res++;
    if (p->next_res == 0u) {
        p->next_res = 1;
    }
    p->res[slot].owner = owner;
    p->res[slot].type = GPU_RES_NONE;
    p->res[slot].state = GPU_ST_ALLOC;
    p->res[slot].dma = -1;
    p->res[slot].ctx_attached = 0;
    p->res[slot].backing_size = 0;
    p->res_live++;
    *id_out = p->res[slot].id;
    return 0;
}

GpuResource *gpu_res_get(GpuPool *p, uint32_t id) {
    uint32_t i;
    if (!p || id == 0u) {
        return 0;
    }
    for (i = 0; i < GPU_RES_MAX; ++i) {
        if (p->res[i].state != GPU_ST_FREE && p->res[i].id == id) {
            return &p->res[i];
        }
    }
    return 0;
}

int gpu_res_release(GpuPool *p, int owner, uint32_t id) {
    GpuResource *r = gpu_res_get(p, id);
    if (!r) {
        return -1;
    }
    if (r->owner != owner) {
        return -2;
    }
    r->state = GPU_ST_FREE;
    r->id = 0;
    r->type = GPU_RES_NONE;
    r->dma = -1;
    r->ctx_attached = 0;
    if (p->res_live > 0) {
        p->res_live--;
    }
    return 0;
}

int gpu_ctx_alloc(GpuPool *p, int owner, uint32_t capset, uint32_t *id_out) {
    uint32_t slot;
    uint32_t i;
    if (!p || !id_out) {
        return -1;
    }
    slot = GPU_CTX_MAX;
    for (i = 0; i < GPU_CTX_MAX; ++i) {
        if (!p->ctx[i].live) {
            slot = i;
            break;
        }
    }
    if (slot == GPU_CTX_MAX) {
        return -1;
    }
    if (p->next_ctx == 0u) {
        p->next_ctx = 1;
    }
    p->ctx[slot].id = p->next_ctx++;
    if (p->next_ctx == 0u) {
        p->next_ctx = 1;
    }
    p->ctx[slot].owner = owner;
    p->ctx[slot].live = 1;
    p->ctx[slot].capset_id = capset;
    p->ctx[slot].fence_next = 1;
    p->ctx_live++;
    *id_out = p->ctx[slot].id;
    return 0;
}

GpuContext *gpu_ctx_get(GpuPool *p, uint32_t id) {
    uint32_t i;
    if (!p || id == 0u) {
        return 0;
    }
    for (i = 0; i < GPU_CTX_MAX; ++i) {
        if (p->ctx[i].live && p->ctx[i].id == id) {
            return &p->ctx[i];
        }
    }
    return 0;
}

int gpu_ctx_release(GpuPool *p, int owner, uint32_t id) {
    GpuContext *c = gpu_ctx_get(p, id);
    if (!c) {
        return -1;
    }
    if (c->owner != owner) {
        return -2;
    }
    c->live = 0;
    c->id = 0;
    if (p->ctx_live > 0) {
        p->ctx_live--;
    }
    return 0;
}

int gpu_res_live(const GpuPool *p) {
    return p ? p->res_live : 0;
}

int gpu_ctx_live(const GpuPool *p) {
    return p ? p->ctx_live : 0;
}
