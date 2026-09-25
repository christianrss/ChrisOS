#include "virtgpu_enc.h"

void vgpu_wr32(uint8_t *p, uint32_t off, uint32_t v) {
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
    p[off + 2] = (uint8_t)(v >> 16);
    p[off + 3] = (uint8_t)(v >> 24);
}

uint32_t vgpu_rd32(const uint8_t *p, uint32_t off) {
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
           ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

void vgpu_hdr(uint8_t *p, uint32_t type, uint32_t flags, uint64_t fence,
              uint32_t ctx) {
    uint32_t i;
    for (i = 0; i < VGPU_HDR_SIZE; ++i) {
        p[i] = 0;
    }
    vgpu_wr32(p, 0, type);
    vgpu_wr32(p, 4, flags);
    vgpu_wr32(p, 8, (uint32_t)fence);
    vgpu_wr32(p, 12, (uint32_t)(fence >> 32));
    vgpu_wr32(p, 16, ctx);
}

static int need(uint32_t cap, uint32_t n) {
    return n > 0u && n <= cap;
}

int vgpu_enc_simple(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t ctx,
                    uint32_t id, uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || !need(cap, 32u)) {
        return -1;
    }
    vgpu_hdr(dst, type, 0, 0, ctx);
    vgpu_wr32(dst, 24, id);
    vgpu_wr32(dst, 28, 0);
    *out_len = 32u;
    return 0;
}

int vgpu_enc_create_2d(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t fmt,
                       uint32_t w, uint32_t h, uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || w == 0u || h == 0u || w > 8192u ||
        h > 8192u || !need(cap, 40u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_RESOURCE_CREATE_2D, 0, 0, 0);
    vgpu_wr32(dst, 24, id);
    vgpu_wr32(dst, 28, fmt);
    vgpu_wr32(dst, 32, w);
    vgpu_wr32(dst, 36, h);
    *out_len = 40u;
    return 0;
}

int vgpu_enc_attach(uint8_t *dst, uint32_t cap, uint32_t id, uint64_t addr,
                    uint32_t length, uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || addr == 0u || length == 0u ||
        !need(cap, 48u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_RESOURCE_ATTACH_BACKING, 0, 0, 0);
    vgpu_wr32(dst, 24, id);
    vgpu_wr32(dst, 28, 1u);
    vgpu_wr32(dst, 32, (uint32_t)addr);
    vgpu_wr32(dst, 36, (uint32_t)(addr >> 32));
    vgpu_wr32(dst, 40, length);
    vgpu_wr32(dst, 44, 0);
    *out_len = 48u;
    return 0;
}

int vgpu_enc_scanout(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t scanout,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || w == 0u || h == 0u || !need(cap, 48u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_SET_SCANOUT, 0, 0, 0);
    vgpu_wr32(dst, 24, x);
    vgpu_wr32(dst, 28, y);
    vgpu_wr32(dst, 32, w);
    vgpu_wr32(dst, 36, h);
    vgpu_wr32(dst, 40, scanout);
    vgpu_wr32(dst, 44, id);
    *out_len = 48u;
    return 0;
}

int vgpu_enc_flush(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t x,
                   uint32_t y, uint32_t w, uint32_t h, uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || w == 0u || h == 0u || !need(cap, 48u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_RESOURCE_FLUSH, 0, 0, 0);
    vgpu_wr32(dst, 24, x);
    vgpu_wr32(dst, 28, y);
    vgpu_wr32(dst, 32, w);
    vgpu_wr32(dst, 36, h);
    vgpu_wr32(dst, 40, id);
    vgpu_wr32(dst, 44, 0);
    *out_len = 48u;
    return 0;
}

int vgpu_enc_xfer2d(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t x,
                    uint32_t y, uint32_t w, uint32_t h, uint64_t off,
                    uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || w == 0u || h == 0u || !need(cap, 56u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_TRANSFER_TO_HOST_2D, 0, 0, 0);
    vgpu_wr32(dst, 24, x);
    vgpu_wr32(dst, 28, y);
    vgpu_wr32(dst, 32, w);
    vgpu_wr32(dst, 36, h);
    vgpu_wr32(dst, 40, (uint32_t)off);
    vgpu_wr32(dst, 44, (uint32_t)(off >> 32));
    vgpu_wr32(dst, 48, id);
    vgpu_wr32(dst, 52, 0);
    *out_len = 56u;
    return 0;
}

int vgpu_enc_create_3d(uint8_t *dst, uint32_t cap, const VgpuCreate3D *info,
                       uint32_t *out_len) {
    if (!dst || !info || !out_len || info->resource_id == 0u ||
        info->width == 0u || info->height == 0u || info->depth == 0u ||
        info->array_size == 0u || info->width > 8192u || info->height > 8192u ||
        info->depth > 8192u || info->array_size > 256u ||
        info->nr_samples > 16u || !need(cap, 72u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_RESOURCE_CREATE_3D, 0, 0, 0);
    vgpu_wr32(dst, 24, info->resource_id);
    vgpu_wr32(dst, 28, info->target);
    vgpu_wr32(dst, 32, info->format);
    vgpu_wr32(dst, 36, info->bind);
    vgpu_wr32(dst, 40, info->width);
    vgpu_wr32(dst, 44, info->height);
    vgpu_wr32(dst, 48, info->depth);
    vgpu_wr32(dst, 52, info->array_size);
    vgpu_wr32(dst, 56, info->last_level);
    vgpu_wr32(dst, 60, info->nr_samples);
    vgpu_wr32(dst, 64, info->flags);
    vgpu_wr32(dst, 68, 0);
    *out_len = 72u;
    return 0;
}

int vgpu_enc_xfer3d(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t ctx,
                    const VgpuXfer3D *box, uint32_t *out_len) {
    if (!dst || !box || !out_len || box->resource_id == 0u || box->w == 0u ||
        box->h == 0u || box->d == 0u ||
        (type != VGPU_CMD_TRANSFER_TO_HOST_3D &&
         type != VGPU_CMD_TRANSFER_FROM_HOST_3D) ||
        !need(cap, 72u)) {
        return -1;
    }
    vgpu_hdr(dst, type, 0, 0, ctx);
    vgpu_wr32(dst, 24, box->x);
    vgpu_wr32(dst, 28, box->y);
    vgpu_wr32(dst, 32, box->z);
    vgpu_wr32(dst, 36, box->w);
    vgpu_wr32(dst, 40, box->h);
    vgpu_wr32(dst, 44, box->d);
    vgpu_wr32(dst, 48, (uint32_t)box->offset);
    vgpu_wr32(dst, 52, (uint32_t)(box->offset >> 32));
    vgpu_wr32(dst, 56, box->resource_id);
    vgpu_wr32(dst, 60, box->level);
    vgpu_wr32(dst, 64, box->stride);
    vgpu_wr32(dst, 68, box->layer_stride);
    *out_len = 72u;
    return 0;
}

int vgpu_enc_ctx_create(uint8_t *dst, uint32_t cap, uint32_t ctx, uint32_t nlen,
                        uint32_t context_init, const char *name,
                        uint32_t *out_len) {
    uint32_t i;
    if (!dst || !out_len || ctx == 0u || !name || nlen > 64u || !need(cap, 96u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_CTX_CREATE, 0, 0, ctx);
    vgpu_wr32(dst, 24, nlen);
    vgpu_wr32(dst, 28, context_init);
    for (i = 0; i < 64u; ++i) {
        dst[32u + i] = 0;
    }
    for (i = 0; i < nlen; ++i) {
        dst[32u + i] = (uint8_t)name[i];
    }
    *out_len = 96u;
    return 0;
}

int vgpu_enc_submit3d(uint8_t *dst, uint32_t cap, uint32_t ctx,
                      const uint32_t *cmds, uint32_t ndwords, uint32_t *out_len) {
    uint32_t i;
    uint32_t bytes;
    uint32_t total;
    if (!dst || !out_len || !cmds || ctx == 0u || ndwords == 0u ||
        ndwords > 4096u) {
        return -1;
    }
    bytes = ndwords * 4u;
    total = 32u + bytes;
    if (!need(cap, total)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_SUBMIT_3D, 0, 0, ctx);
    vgpu_wr32(dst, 24, bytes);
    vgpu_wr32(dst, 28, 0);
    for (i = 0; i < ndwords; ++i) {
        vgpu_wr32(dst, 32u + i * 4u, cmds[i]);
    }
    *out_len = total;
    return 0;
}

int vgpu_enc_capset_info(uint8_t *dst, uint32_t cap, uint32_t index,
                         uint32_t *out_len) {
    if (!dst || !out_len || !need(cap, 32u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_GET_CAPSET_INFO, 0, 0, 0);
    vgpu_wr32(dst, 24, index);
    vgpu_wr32(dst, 28, 0);
    *out_len = 32u;
    return 0;
}

int vgpu_enc_capset(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t version,
                    uint32_t *out_len) {
    if (!dst || !out_len || id == 0u || !need(cap, 32u)) {
        return -1;
    }
    vgpu_hdr(dst, VGPU_CMD_GET_CAPSET, 0, 0, 0);
    vgpu_wr32(dst, 24, id);
    vgpu_wr32(dst, 28, version);
    *out_len = 32u;
    return 0;
}

int vgpu_enc_cursor(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t res,
                    uint32_t x, uint32_t y, uint32_t hot_x, uint32_t hot_y,
                    uint32_t *out_len) {
    if (!dst || !out_len ||
        (type != VGPU_CMD_UPDATE_CURSOR && type != VGPU_CMD_MOVE_CURSOR) ||
        !need(cap, 56u)) {
        return -1;
    }
    if (type == VGPU_CMD_UPDATE_CURSOR && res == 0u) {
        return -1;
    }
    vgpu_hdr(dst, type, 0, 0, 0);
    vgpu_wr32(dst, 24, 0);
    vgpu_wr32(dst, 28, x);
    vgpu_wr32(dst, 32, y);
    vgpu_wr32(dst, 36, 0);
    vgpu_wr32(dst, 40, res);
    vgpu_wr32(dst, 44, hot_x);
    vgpu_wr32(dst, 48, hot_y);
    vgpu_wr32(dst, 52, 0);
    *out_len = 56u;
    return 0;
}
