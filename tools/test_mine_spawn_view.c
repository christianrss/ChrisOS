#include <stdio.h>
#include "math3d.h"

static int g_fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("fail: %s\n", msg);
        g_fail = 1;
    }
}

int main(void) {
    Mat4f view;
    Vec3f eye;
    Vec3f world;
    int sx;
    int sy;
    uint32_t sz;
    int ok;

    g_fail = 0;
    math3d_cam_set(72.0f, 11.0f, 80.0f, 0.0f, 0.0f);
    math3d_set_screen(800, 600);
    math3d_view(&view);

    /* Ground block one step in front (-Z), top surface y=8 */
    vec3f_set(&world, 72.5f, 8.0f, 79.5f);
    mat4f_transform(&view, &world, &eye);
    ok = project_view(eye.x, eye.y, eye.z, &sx, &sy, &sz);
    printf("ground in front: eye=(%.3f,%.3f,%.3f) sy=%d (horizon=300)\n",
           eye.x, eye.y, eye.z, sy);
    expect(ok == 1, "ground projects");
    expect(sy > 300, "ground should be in lower half at spawn");

    /* Sky point: high above horizon in view */
    vec3f_set(&world, 72.5f, 40.0f, 79.5f);
    mat4f_transform(&view, &world, &eye);
    ok = project_view(eye.x, eye.y, eye.z, &sx, &sy, &sz);
    printf("high point: sy=%d\n", sy);
    expect(ok == 1, "sky point projects");
    expect(sy < 300, "high point should be upper half");

    if (g_fail)
        return 1;
    puts("test_mine_spawn_view: ok");
    return 0;
}
