#include "gfx3d_batch.h"

int gfx3d_batch_open(Gfx3DBatch *b, uint32_t ctx,
                     int (*submit)(void *, uint32_t, const uint32_t *, uint32_t), void *user) {
    if (!b || ctx == 0u) {
        return -1;
    }
    b->ctx = ctx;
    b->submit = submit;
    b->user = user;
    b->submits = 0;
    b->dwords = 0;
    b->err = 0;
    return virgl_cmd_init(&b->cmd, b->d, GFX3D_BATCH_DWORDS, ctx);
}

int gfx3d_batch_flush(Gfx3DBatch *b) {
    uint32_t n;
    if (!b || b->err) {
        return -1;
    }
    n = virgl_cmd_len(&b->cmd);
    if (n == 0u) {
        return 0;
    }
    if (b->cmd.expect != 0u || !virgl_cmd_ok(&b->cmd)) {
        b->err = -1;
        return -1;
    }
    if (!b->submit || b->submit(b->user, b->ctx, b->d, n) != 0) {
        b->err = -1;
        return -1;
    }
    b->submits++;
    b->dwords += n;
    return virgl_cmd_init(&b->cmd, b->d, GFX3D_BATCH_DWORDS, b->ctx);
}

int gfx3d_batch_reserve(Gfx3DBatch *b, uint32_t need) {
    if (!b || b->err || need == 0u || need > GFX3D_BATCH_DWORDS) {
        if (b) {
            b->err = -1;
        }
        return -1;
    }
    if (b->cmd.n + need > b->cmd.cap) {
        if (gfx3d_batch_flush(b) != 0) {
            return -1;
        }
    }
    if (b->cmd.n + need > b->cmd.cap) {
        b->err = -1;
        return -1;
    }
    return 0;
}

VirglCmd *gfx3d_batch_cmd(Gfx3DBatch *b) {
    return b ? &b->cmd : 0;
}

uint32_t gfx3d_batch_submits(const Gfx3DBatch *b) {
    return b ? b->submits : 0u;
}
