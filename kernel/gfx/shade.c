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

static Vec3f g_Ln = { 0.0f, 1.0f, 0.0f };
static Vec3f g_Vn = { 0.0f, 0.0f, 1.0f };
static Vec3f g_cam_cached;
static int g_lv_ready;

static void refresh_lv(void) {
    Vec3f cam;
    g_Ln = g_light_pos;
    vec3f_norm(&g_Ln);
    math3d_cam_get(&cam, 0, 0);
    g_cam_cached = cam;
    vec3f_set(&g_Vn, -cam.x, -cam.y, -cam.z);
    vec3f_norm(&g_Vn);
    g_lv_ready = 1;
}

void shade_set_light(float x, float y, float z, float r, float g, float b) {
    vec3f_set(&g_light_pos, x, y, z);
    vec3f_set(&g_light_col, r, g, b);
    g_lv_ready = 0;
}

void shade_state_save(ShadeState *out) {
    if (!out) {
        return;
    }
    out->pos = g_light_pos;
    out->col = g_light_col;
}

void shade_state_load(const ShadeState *in) {
    if (!in) {
        return;
    }
    g_light_pos = in->pos;
    g_light_col = in->col;
    g_lv_ready = 0;
}

void shade_get_light(Vec3f *pos, Vec3f *col) {
    if (pos)
        *pos = g_light_pos;
    if (col)
        *col = g_light_col;
}

uint32_t shade_phong(uint32_t rgb, float nx, float ny, float nz) {
    Vec3f n;
    Vec3f h;
    Vec3f cam;
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

    vec3f_set(&n, nx, ny, nz);
    vec3f_norm(&n);
    math3d_cam_get(&cam, 0, 0);
    if (!g_lv_ready || cam.x != g_cam_cached.x || cam.y != g_cam_cached.y ||
        cam.z != g_cam_cached.z) {
        refresh_lv();
    }
    vec3f_add(&h, &g_Ln, &g_Vn);
    vec3f_norm(&h);
    diff = clampf(vec3f_dot(&n, &g_Ln), 0.0f, 1.0f);
    spec = pow16(clampf(vec3f_dot(&n, &h), 0.0f, 1.0f));
    cr = cr * (ka + kd * diff * g_light_col.x) + 255.0f * ks * spec * g_light_col.x;
    cg = cg * (ka + kd * diff * g_light_col.y) + 255.0f * ks * spec * g_light_col.y;
    cb = cb * (ka + kd * diff * g_light_col.z) + 255.0f * ks * spec * g_light_col.z;
    ir = (int)clampf(cr, 0.0f, 255.0f);
    ig = (int)clampf(cg, 0.0f, 255.0f);
    ib = (int)clampf(cb, 0.0f, 255.0f);
    return ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}
