#include "shade.h"

static Vec3f g_light_pos = { 2.5f, 6.0f, 4.0f };
static Vec3f g_light_col = { 1.0f, 1.0f, 1.0f };

static float clampf(float v, float lo, float hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static float pow16(float x) {
    float x2;
    float x4;
    float x8;
    if (x <= 0.0f)
        return 0.0f;
    x2 = x * x;
    x4 = x2 * x2;
    x8 = x4 * x4;
    return x8 * x8;
}

void shade_set_light(float x, float y, float z, float r, float g, float b) {
    vec3f_set(&g_light_pos, x, y, z);
    vec3f_set(&g_light_col, r, g, b);
}

void shade_get_light(Vec3f *pos, Vec3f *col) {
    if (pos)
        *pos = g_light_pos;
    if (col)
        *col = g_light_col;
}

uint32_t shade_phong(uint32_t rgb, float nx, float ny, float nz) {
    Vec3f n;
    Vec3f l;
    Vec3f v;
    Vec3f h;
    float diff;
    float spec;
    float ka = 0.22f;
    float kd = 0.70f;
    float ks = 0.28f;
    float cr = (float)((rgb >> 16) & 0xffu);
    float cg = (float)((rgb >> 8) & 0xffu);
    float cb = (float)(rgb & 0xffu);
    int ir;
    int ig;
    int ib;
    Vec3f cam;

    vec3f_set(&n, nx, ny, nz);
    vec3f_norm(&n);
    l = g_light_pos;
    vec3f_norm(&l);
    math3d_cam_get(&cam, 0, 0);
    vec3f_set(&v, -cam.x, -cam.y, -cam.z);
    vec3f_norm(&v);
    vec3f_add(&h, &l, &v);
    vec3f_norm(&h);
    diff = clampf(vec3f_dot(&n, &l), 0.0f, 1.0f);
    spec = pow16(clampf(vec3f_dot(&n, &h), 0.0f, 1.0f));
    cr = cr * (ka + kd * diff * g_light_col.x) + 255.0f * ks * spec * g_light_col.x;
    cg = cg * (ka + kd * diff * g_light_col.y) + 255.0f * ks * spec * g_light_col.y;
    cb = cb * (ka + kd * diff * g_light_col.z) + 255.0f * ks * spec * g_light_col.z;
    ir = (int)clampf(cr, 0.0f, 255.0f);
    ig = (int)clampf(cg, 0.0f, 255.0f);
    ib = (int)clampf(cb, 0.0f, 255.0f);
    return ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}
