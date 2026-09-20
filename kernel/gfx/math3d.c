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

void mat4f_translate(Mat4f *o, float x, float y, float z) {
    mat4f_identity(o);
    o->m[3] = x;
    o->m[7] = y;
    o->m[11] = z;
}

void mat4f_transform(const Mat4f *m, const Vec3f *in, Vec3f *out) {
    float x = in->x;
    float y = in->y;
    float z = in->z;
    out->x = m->m[0] * x + m->m[1] * y + m->m[2] * z + m->m[3];
    out->y = m->m[4] * x + m->m[5] * y + m->m[6] * z + m->m[7];
    out->z = m->m[8] * x + m->m[9] * y + m->m[10] * z + m->m[11];
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

void math3d_view(Mat4f *o) {
    float cy = gfx_cosf(g_cam_yaw);
    float sy = gfx_sinf(g_cam_yaw);
    float cp = gfx_cosf(g_cam_pitch);
    float sp = gfx_sinf(g_cam_pitch);
    Vec3f f;
    Vec3f r;
    Vec3f u;
    Vec3f world_up;

    f.x = sy * cp;
    f.y = -sp;
    f.z = -cy * cp;
    vec3f_norm(&f);
    vec3f_set(&world_up, 0.0f, 1.0f, 0.0f);
    vec3f_cross(&r, &world_up, &f);
    if (vec3f_len(&r) < 1e-6f)
        vec3f_set(&r, 1.0f, 0.0f, 0.0f);
    else
        vec3f_norm(&r);
    vec3f_cross(&u, &f, &r);
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
