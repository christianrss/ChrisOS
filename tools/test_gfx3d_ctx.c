#include "gfx3d_ctx.h"
#include "voxel.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "fail: %s\n", msg);
    return 1;
}

int main(void) {
    Gfx3DCtx a;
    Gfx3DCtx b;
    Vec3f pos;
    float yaw;
    float pitch;
    Vec3f lpos;
    Vec3f lcol;

    math3d_cam_set(9.0f, 9.0f, 9.0f, 30.0f, 12.0f);
    shade_set_light(3.0f, 3.0f, 3.0f, 0.1f, 0.2f, 0.3f);
    tex_set_slot(7);
    tex_ofs(0.9f, 0.8f);
    math3d_set_screen(640, 480);
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    gfx3d_ctx_load(&a);
    math3d_cam_get(&pos, &yaw, &pitch);
    shade_get_light(&lpos, &lcol);
    if (pos.x != 0.0f || pos.y != 1.5f || pos.z != 5.0f || yaw != 0.0f ||
        pitch != 0.0f) {
        return fail("first load copied a foreign camera");
    }
    if (lpos.x != 2.5f || lpos.y != 6.0f || lcol.x != 1.0f || tex_slot() != 1) {
        return fail("first load copied a foreign light or texture");
    }
    if (math3d_screen_w() != 640 || math3d_screen_h() != 480) {
        return fail("first load dropped the viewport size");
    }
    math3d_cam_set(1.0f, 2.0f, 3.0f, 10.0f, -4.0f);
    shade_set_light(9.0f, 8.0f, 7.0f, 0.2f, 0.3f, 0.4f);
    tex_set_slot(3);
    tex_ofs(0.25f, 0.5f);
    gfx3d_ctx_save(&a);

    math3d_cam_set(4.0f, 5.0f, 6.0f, 20.0f, 8.0f);
    shade_set_light(1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    tex_set_slot(1);
    gfx3d_ctx_save(&b);

    gfx3d_ctx_load(&a);
    math3d_cam_get(&pos, &yaw, &pitch);
    shade_get_light(&lpos, &lcol);
    if (pos.x != 1.0f || pos.y != 2.0f || yaw != 10.0f || pitch != -4.0f) {
        return fail("camera A");
    }
    if (lpos.x != 9.0f || lcol.z != 0.4f || tex_slot() != 3) {
        return fail("light/tex A");
    }
    gfx3d_ctx_load(&b);
    math3d_cam_get(&pos, &yaw, &pitch);
    if (pos.x != 4.0f || yaw != 20.0f || pitch != 8.0f) {
        return fail("camera B leaked from A");
    }
    if (voxel_claim(1) != 0 || voxel_claim(2) == 0) {
        return fail("voxel owner");
    }
    if (voxel_claim(1) != 0) {
        return fail("owner reenter");
    }
    voxel_release(1);
    if (voxel_claim(2) != 0) {
        return fail("release");
    }
    voxel_release(2);
    puts("test_gfx3d_ctx: ok");
    return 0;
}
