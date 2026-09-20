#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "math3d.h"

static int near_f(float a, float b) {
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 1e-3f;
}

int main(void) {
    Mat4f id;
    Mat4f rot;
    int sx;
    int sy;
    uint32_t sz;
    int ok;

    math3d_set_screen(320, 200);
    mat4f_identity(&id);

    ok = project_vertex(&id, 0.0f, 0.0f, 2.0f, &sx, &sy, &sz);
    assert(ok == 1);
    assert(sx == 160);
    assert(sy == 100);
    assert(sz == depth_to_z(2.0f));

    ok = project_vertex(&id, 1.0f, 0.0f, 2.0f, &sx, &sy, &sz);
    assert(ok == 1);
    assert(sx == 240);
    assert(sy == 100);

    ok = project_vertex(&id, 0.0f, 0.0f, -1.0f, &sx, &sy, &sz);
    assert(ok == 0);

    mat4f_rotate_y(&rot, 0.0f);
    ok = project_vertex(&rot, 1.0f, 0.0f, 2.0f, &sx, &sy, &sz);
    assert(ok == 1);
    assert(sx == 240);

    mat4f_rotate_y(&rot, 90.0f);
    {
        Vec3f in;
        Vec3f out;
        vec3f_set(&in, 1.0f, 0.0f, 0.0f);
        mat4f_transform(&rot, &in, &out);
        assert(near_f(out.x, 0.0f));
        assert(near_f(out.z, 1.0f));
    }

    assert(near_f(gfx_sinf(0.0f), 0.0f));
    assert(near_f(gfx_sinf(90.0f), 1.0f));
    assert(near_f(gfx_cosf(0.0f), 1.0f));
    puts("test_math3d: ok");
    return 0;
}
