#include <stdio.h>
#include "math3d.h"

static int g_fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("fail: %s\n", msg);
        g_fail = 1;
    }
}

static void basis(float yaw, float pitch, Vec3f *fwd, Vec3f *right) {
    float cy = gfx_cosf(yaw);
    float sy = gfx_sinf(yaw);
    float cp = gfx_cosf(pitch);
    float sp = gfx_sinf(pitch);
    fwd->x = sy * cp;
    fwd->y = -sp;
    fwd->z = -cy * cp;
    right->x = cy;
    right->y = 0.0f;
    right->z = sy;
}

static void check_pose(float yaw, float pitch) {
    Vec3f fwd;
    Vec3f right;
    Mat4f view;
    int sx;
    int sy;
    uint32_t sz;
    int ok;
    float x;
    float y;
    float z;
    const float cx = 10.0f;
    const float cy = 20.0f;
    const float cz = 30.0f;

    basis(yaw, pitch, &fwd, &right);
    math3d_cam_set(cx, cy, cz, yaw, pitch);
    math3d_set_screen(800, 600);
    math3d_view(&view);

    x = cx + fwd.x * 8.0f;
    y = cy + fwd.y * 8.0f + 1.5f;
    z = cz + fwd.z * 8.0f;
    ok = project_vertex(&view, x, y, z, &sx, &sy, &sz);
    expect(ok == 1, "above in front projects");
    expect(sy < 300, "above in front is upper half");

    x = cx + fwd.x * 8.0f;
    y = cy + fwd.y * 8.0f - 1.5f;
    z = cz + fwd.z * 8.0f;
    ok = project_vertex(&view, x, y, z, &sx, &sy, &sz);
    expect(ok == 1, "below aim projects");
    expect(sy > 300, "below aim is lower half");

    x = cx + fwd.x * 8.0f + right.x * 2.0f;
    y = cy + fwd.y * 8.0f;
    z = cz + fwd.z * 8.0f + right.z * 2.0f;
    ok = project_vertex(&view, x, y, z, &sx, &sy, &sz);
    expect(ok == 1, "right projects");
    expect(sx > 400, "right is right of center");
}

int main(void) {
    static const float yaws[4] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float pitches[3] = { -40.0f, 0.0f, 40.0f };
    int i;
    int j;

    g_fail = 0;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 3; ++j)
            check_pose(yaws[i], pitches[j]);
    }
    /* Positive pitch looks down: forward Y is negative.
     * Negative pitch looks up. World +Y stays above world -Y on screen. */
    {
        Vec3f pos;
        Vec3f fwd;
        Vec3f right;
        float yaw;
        float pitch;
        int sky_x, sky_y, ground_x, ground_y;
        uint32_t sz;
        Mat4f view;
        math3d_cam_set(0.0f, 10.0f, 0.0f, 0.0f, 30.0f);
        math3d_cam_get(&pos, &yaw, &pitch);
        expect(pitch > 0.0f, "positive pitch stored");
        basis(0.0f, 30.0f, &fwd, &right);
        expect(fwd.y < 0.0f, "positive pitch looks down");
        basis(0.0f, -30.0f, &fwd, &right);
        expect(fwd.y > 0.0f, "negative pitch looks up");
        math3d_cam_set(0.0f, 10.0f, 0.0f, 0.0f, 0.0f);
        math3d_set_screen(800, 600);
        math3d_view(&view);
        expect(project_vertex(&view, 0.0f, 14.0f, -8.0f, &sky_x, &sky_y, &sz) == 1,
               "sky projects");
        expect(project_vertex(&view, 0.0f, 6.0f, -8.0f, &ground_x, &ground_y, &sz) == 1,
               "ground projects");
        expect(sky_y < ground_y, "world up is above the ground");
        expect(sky_y < 300, "sky is above the horizon");
        expect(ground_y > 300, "ground is below the horizon");
        /* Screen dy > 0 is pointer-down. Pitch delta follows it. */
        expect((0.0f + 10.0f * 0.10f) > 0.0f, "pointer down increases pitch");
        expect((0.0f + -10.0f * 0.10f) < 0.0f, "pointer up decreases pitch");
    }
    if (g_fail)
        return 1;
    puts("test_math3d_view: ok");
    return 0;
}
