#include "math3d.h"

static int g_sw = 320;
static int g_sh = 200;

static const float sin_lut[91] = {
    0.0f, 0.017452f, 0.034899f, 0.052336f, 0.069756f, 0.087156f,
    0.104528f, 0.121869f, 0.139173f, 0.156434f, 0.173648f, 0.190809f,
    0.207912f, 0.224951f, 0.241922f, 0.258819f, 0.275637f, 0.292372f,
    0.309017f, 0.325568f, 0.342020f, 0.358368f, 0.374607f, 0.390731f,
    0.406737f, 0.422618f, 0.438371f, 0.453990f, 0.469472f, 0.484810f,
    0.500000f, 0.515038f, 0.529919f, 0.544639f, 0.559193f, 0.573576f,
    0.587785f, 0.601815f, 0.615661f, 0.629320f, 0.642788f, 0.656059f,
    0.669131f, 0.681998f, 0.694658f, 0.707107f, 0.719340f, 0.731354f,
    0.743145f, 0.754710f, 0.766044f, 0.777146f, 0.788011f, 0.798636f,
    0.809017f, 0.819152f, 0.829038f, 0.838671f, 0.848048f, 0.857167f,
    0.866025f, 0.874620f, 0.882948f, 0.891007f, 0.898794f, 0.906308f,
    0.913545f, 0.920505f, 0.927184f, 0.933580f, 0.939693f, 0.945519f,
    0.951057f, 0.956305f, 0.961262f, 0.965926f, 0.970296f, 0.974370f,
    0.978148f, 0.981627f, 0.984808f, 0.987688f, 0.990268f, 0.992546f,
    0.994522f, 0.996195f, 0.997564f, 0.998630f, 0.999391f, 0.999848f,
    1.0f
};

static float wrap_deg(float deg) {
    while (deg < 0.0f)
        deg += 360.0f;
    while (deg >= 360.0f)
        deg -= 360.0f;
    return deg;
}

float gfx_sinf(float deg) {
    float d = wrap_deg(deg);
    int i = (int)d;
    if (i <= 90)
        return sin_lut[i];
    if (i <= 180)
        return sin_lut[180 - i];
    if (i <= 270)
        return -sin_lut[i - 180];
    return -sin_lut[360 - i];
}

float gfx_cosf(float deg) {
    return gfx_sinf(deg + 90.0f);
}

uint32_t gfx_sinf_bits(uint32_t deg_bits) {
    union {
        uint32_t u;
        float f;
    } in;
    union {
        uint32_t u;
        float f;
    } out;
    in.u = deg_bits;
    out.f = gfx_sinf(in.f);
    return out.u;
}

uint32_t gfx_cosf_bits(uint32_t deg_bits) {
    union {
        uint32_t u;
        float f;
    } in;
    union {
        uint32_t u;
        float f;
    } out;
    in.u = deg_bits;
    out.f = gfx_cosf(in.f);
    return out.u;
}

static Vec3f g_cam_pos = { 0.0f, 1.5f, 5.0f };
static float g_cam_yaw;
static float g_cam_pitch;

void vec3f_set(Vec3f *v, float x, float y, float z) {
    v->x = x;
    v->y = y;
    v->z = z;
}

void vec3f_add(Vec3f *o, const Vec3f *a, const Vec3f *b) {
    o->x = a->x + b->x;
    o->y = a->y + b->y;
    o->z = a->z + b->z;
}

void vec3f_sub(Vec3f *o, const Vec3f *a, const Vec3f *b) {
    o->x = a->x - b->x;
    o->y = a->y - b->y;
    o->z = a->z - b->z;
}

float vec3f_dot(const Vec3f *a, const Vec3f *b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

void vec3f_cross(Vec3f *o, const Vec3f *a, const Vec3f *b) {
    o->x = a->y * b->z - a->z * b->y;
    o->y = a->z * b->x - a->x * b->z;
    o->z = a->x * b->y - a->y * b->x;
}

float vec3f_len(const Vec3f *v) {
    float s = vec3f_dot(v, v);
    if (s <= 0.0f)
        return 0.0f;
    return __builtin_sqrtf(s);
}

void vec3f_norm(Vec3f *v) {
    float n = vec3f_len(v);
    if (n <= 1e-8f) {
        v->x = 0.0f;
        v->y = 1.0f;
        v->z = 0.0f;
        return;
    }
    v->x /= n;
    v->y /= n;
    v->z /= n;
}

void mat4f_identity(Mat4f *o) {
    int i;
    for (i = 0; i < 16; ++i)
        o->m[i] = 0.0f;
    o->m[0] = 1.0f;
    o->m[5] = 1.0f;
    o->m[10] = 1.0f;
    o->m[15] = 1.0f;
}

void mat4f_mul(Mat4f *o, const Mat4f *a, const Mat4f *b) {
    Mat4f t;
    int row;
    int col;
    int k;
    for (row = 0; row < 4; ++row) {
        for (col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (k = 0; k < 4; ++k)
                sum += a->m[row * 4 + k] * b->m[k * 4 + col];
            t.m[row * 4 + col] = sum;
        }
    }
    *o = t;
}

void mat4f_rotate_x(Mat4f *o, float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    mat4f_identity(o);
    o->m[5] = c;
    o->m[6] = -s;
    o->m[9] = s;
    o->m[10] = c;
}

void mat4f_rotate_y(Mat4f *o, float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    mat4f_identity(o);
    o->m[0] = c;
    o->m[2] = -s;
    o->m[8] = s;
    o->m[10] = c;
}

void mat4f_rotate_z(Mat4f *o, float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    mat4f_identity(o);
    o->m[0] = c;
    o->m[1] = -s;
    o->m[4] = s;
    o->m[5] = c;
}

void mat4f_scale(Mat4f *o, float x, float y, float z) {
    mat4f_identity(o);
    o->m[0] = x;
    o->m[5] = y;
    o->m[10] = z;
}

void mat4f_translate(Mat4f *o, float x, float y, float z) {
    mat4f_identity(o);
    o->m[3] = x;
    o->m[7] = y;
    o->m[11] = z;
}

void mat4f_perspective(Mat4f *o, float fov_deg, float aspect, float znear, float zfar) {
    float f;
    float span;
    if (aspect < 1e-6f)
        aspect = 1.0f;
    if (znear < 1e-4f)
        znear = 0.1f;
    if (zfar <= znear)
        zfar = znear + 1.0f;
    {
        float s = gfx_sinf(fov_deg * 0.5f);
        float c = gfx_cosf(fov_deg * 0.5f);
        if (s < 1e-4f && s > -1e-4f)
            s = 1.0f;
        f = c / s;
    }
    if (f < 0.0f)
        f = -f;
    span = znear - zfar;
    mat4f_identity(o);
    o->m[0] = f / aspect;
    o->m[5] = f;
    o->m[10] = (zfar + znear) / span;
    o->m[11] = (2.0f * zfar * znear) / span;
    o->m[14] = -1.0f;
    o->m[15] = 0.0f;
}

void mat4f_ortho(Mat4f *o, float left, float right, float bottom, float top, float znear,
                 float zfar) {
    float rl = right - left;
    float tb = top - bottom;
    float fn = zfar - znear;
    if (rl < 1e-6f && rl > -1e-6f)
        rl = 1.0f;
    if (tb < 1e-6f && tb > -1e-6f)
        tb = 1.0f;
    if (fn < 1e-6f && fn > -1e-6f)
        fn = 1.0f;
    mat4f_identity(o);
    o->m[0] = 2.0f / rl;
    o->m[5] = 2.0f / tb;
    o->m[10] = -2.0f / fn;
    o->m[3] = -(right + left) / rl;
    o->m[7] = -(top + bottom) / tb;
    o->m[11] = -(zfar + znear) / fn;
}

void mat4f_transform(const Mat4f *m, const Vec3f *in, Vec3f *out) {
    float x = in->x;
    float y = in->y;
    float z = in->z;
    out->x = m->m[0] * x + m->m[1] * y + m->m[2] * z + m->m[3];
    out->y = m->m[4] * x + m->m[5] * y + m->m[6] * z + m->m[7];
    out->z = m->m[8] * x + m->m[9] * y + m->m[10] * z + m->m[11];
}

void mat4f_transform4(const Mat4f *m, float x, float y, float z, float w, float out[4]) {
    out[0] = m->m[0] * x + m->m[1] * y + m->m[2] * z + m->m[3] * w;
    out[1] = m->m[4] * x + m->m[5] * y + m->m[6] * z + m->m[7] * w;
    out[2] = m->m[8] * x + m->m[9] * y + m->m[10] * z + m->m[11] * w;
    out[3] = m->m[12] * x + m->m[13] * y + m->m[14] * z + m->m[15] * w;
}

void mat4f_to_glsl(const Mat4f *m, float out[16]) {
    int row;
    int col;
    for (col = 0; col < 4; ++col) {
        for (row = 0; row < 4; ++row)
            out[col * 4 + row] = m->m[row * 4 + col];
    }
}

void mat4f_normal3(const Mat4f *model, float out9[9]) {
    float g[16];
    mat4f_to_glsl(model, g);
    out9[0] = g[0];
    out9[1] = g[1];
    out9[2] = g[2];
    out9[3] = g[4];
    out9[4] = g[5];
    out9[5] = g[6];
    out9[6] = g[8];
    out9[7] = g[9];
    out9[8] = g[10];
}

void mat4f_transform_dir(const Mat4f *m, const Vec3f *in, Vec3f *out) {
    float x = in->x;
    float y = in->y;
    float z = in->z;
    out->x = m->m[0] * x + m->m[1] * y + m->m[2] * z;
    out->y = m->m[4] * x + m->m[5] * y + m->m[6] * z;
    out->z = m->m[8] * x + m->m[9] * y + m->m[10] * z;
}

void math3d_cam_set(float x, float y, float z, float yaw, float pitch) {
    g_cam_pos.x = x;
    g_cam_pos.y = y;
    g_cam_pos.z = z;
    g_cam_yaw = yaw;
    g_cam_pitch = pitch;
}

void math3d_cam_reset(void) {
    math3d_cam_set(0.0f, 1.5f, 5.0f, 0.0f, 0.0f);
}

void math3d_cam_get(Vec3f *pos, float *yaw, float *pitch) {
    if (pos)
        *pos = g_cam_pos;
    if (yaw)
        *yaw = g_cam_yaw;
    if (pitch)
        *pitch = g_cam_pitch;
}

void math3d_state_save(Gfx3DView *out) {
    if (!out) {
        return;
    }
    out->pos = g_cam_pos;
    out->yaw = g_cam_yaw;
    out->pitch = g_cam_pitch;
    out->screen_w = g_sw;
    out->screen_h = g_sh;
}

void math3d_state_load(const Gfx3DView *in) {
    if (!in) {
        return;
    }
    g_cam_pos = in->pos;
    g_cam_yaw = in->yaw;
    g_cam_pitch = in->pitch;
    g_sw = in->screen_w > 0 ? in->screen_w : 1;
    g_sh = in->screen_h > 0 ? in->screen_h : 1;
}

void math3d_view(Mat4f *o) {
    float cy = gfx_cosf(g_cam_yaw);
    float sy = gfx_sinf(g_cam_yaw);
    float cp = gfx_cosf(g_cam_pitch);
    float sp = gfx_sinf(g_cam_pitch);
    Vec3f f;
    Vec3f r;
    Vec3f u;

    /* Yaw 0 looks toward -Z. Positive pitch looks down. Right is +X, no roll. */
    f.x = sy * cp;
    f.y = -sp;
    f.z = -cy * cp;
    r.x = cy;
    r.y = 0.0f;
    r.z = sy;
    u.x = r.y * f.z - r.z * f.y;
    u.y = r.z * f.x - r.x * f.z;
    u.z = r.x * f.y - r.y * f.x;
    vec3f_norm(&f);
    vec3f_norm(&u);
    mat4f_identity(o);
    o->m[0] = r.x;
    o->m[1] = r.y;
    o->m[2] = r.z;
    o->m[3] = -vec3f_dot(&r, &g_cam_pos);
    o->m[4] = u.x;
    o->m[5] = u.y;
    o->m[6] = u.z;
    o->m[7] = -vec3f_dot(&u, &g_cam_pos);
    o->m[8] = f.x;
    o->m[9] = f.y;
    o->m[10] = f.z;
    o->m[11] = -vec3f_dot(&f, &g_cam_pos);
}

uint32_t depth_to_z(float clip_z) {
    if (clip_z <= 0.0f)
        return 0xFFFFFFFFu;
    if (clip_z > 1000000.0f)
        return 0xFFFFFFFFu;
    return (uint32_t)(clip_z * 65536.0f);
}

void math3d_set_screen(int width, int height) {
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    g_sw = width;
    g_sh = height;
}

int math3d_screen_w(void) {
    return g_sw;
}

int math3d_screen_h(void) {
    return g_sh;
}

int project_view(float x, float y, float z, int *sx, int *sy, uint32_t *sz) {
    float hz;
    int half_w;
    int half_h;

    if (z <= 0.0f) {
        *sx = -1;
        *sy = -1;
        *sz = 0xFFFFFFFFu;
        return 0;
    }
    half_w = g_sw / 2;
    half_h = g_sh / 2;
    hz = z;
    *sx = half_w + (int)(x * (float)half_w / hz);
    *sy = half_h - (int)(y * (float)half_h / hz);
    *sz = depth_to_z(z);
    return 1;
}

int project_vertex(const Mat4f *mvp, float x, float y, float z,
                   int *sx, int *sy, uint32_t *sz) {
    Vec3f in;
    Vec3f out;
    float hz;
    int half_w;
    int half_h;

    vec3f_set(&in, x, y, z);
    mat4f_transform(mvp, &in, &out);
    if (out.z <= 0.0f) {
        *sx = -1;
        *sy = -1;
        *sz = 0xFFFFFFFFu;
        return 0;
    }
    half_w = g_sw / 2;
    half_h = g_sh / 2;
    hz = out.z;
    *sx = half_w + (int)(out.x * (float)half_w / hz);
    *sy = half_h - (int)(out.y * (float)half_h / hz);
    *sz = depth_to_z(out.z);
    return 1;
}
