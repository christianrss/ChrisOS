#include "gfx3d_ctx.h"

/* Camera, light, and texture start from the same values as the gfx statics.
 * Screen size stays whatever the viewport just installed: that is the window,
 * not another app's camera. */
static void gfx3d_ctx_prime(Gfx3DCtx *ctx) {
    int sw = math3d_screen_w();
    int sh = math3d_screen_h();
    ctx->view.pos.x = 0.0f;
    ctx->view.pos.y = 1.5f;
    ctx->view.pos.z = 5.0f;
    ctx->view.yaw = 0.0f;
    ctx->view.pitch = 0.0f;
    ctx->view.screen_w = sw > 0 ? sw : 320;
    ctx->view.screen_h = sh > 0 ? sh : 200;
    ctx->shade.pos.x = 2.5f;
    ctx->shade.pos.y = 6.0f;
    ctx->shade.pos.z = 4.0f;
    ctx->shade.col.x = 1.0f;
    ctx->shade.col.y = 1.0f;
    ctx->shade.col.z = 1.0f;
    ctx->tex.slot = 1;
    ctx->tex.du = 0.0f;
    ctx->tex.dv = 0.0f;
    ctx->valid = 1;
}

void gfx3d_ctx_load(Gfx3DCtx *ctx) {
    if (!ctx) {
        return;
    }
    if (!ctx->valid) {
        gfx3d_ctx_prime(ctx);
    }
    math3d_state_load(&ctx->view);
    shade_state_load(&ctx->shade);
    tex_state_load(&ctx->tex);
}

void gfx3d_ctx_save(Gfx3DCtx *ctx) {
    if (!ctx) {
        return;
    }
    math3d_state_save(&ctx->view);
    shade_state_save(&ctx->shade);
    tex_state_save(&ctx->tex);
    ctx->valid = 1;
}
