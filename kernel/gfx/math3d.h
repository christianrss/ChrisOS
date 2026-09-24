#ifndef CHRIS_MATH3D_H
#define CHRIS_MATH3D_H

#include <stdint.h>

#define MODEL_SCALE 1000.0f

typedef struct {
    float x;
    float y;
    float z;
} Vec3f;

typedef struct {
    float m[16];
} Mat4f;

float gfx_sinf(float deg);
float gfx_cosf(float deg);
uint32_t gfx_sinf_bits(uint32_t deg_bits);
uint32_t gfx_cosf_bits(uint32_t deg_bits);

void vec3f_set(Vec3f *v, float x, float y, float z);
void vec3f_add(Vec3f *o, const Vec3f *a, const Vec3f *b);
void vec3f_sub(Vec3f *o, const Vec3f *a, const Vec3f *b);
float vec3f_dot(const Vec3f *a, const Vec3f *b);
void vec3f_cross(Vec3f *o, const Vec3f *a, const Vec3f *b);
float vec3f_len(const Vec3f *v);
void vec3f_norm(Vec3f *v);

void mat4f_identity(Mat4f *o);
void mat4f_mul(Mat4f *o, const Mat4f *a, const Mat4f *b);
void mat4f_rotate_x(Mat4f *o, float deg);
void mat4f_rotate_y(Mat4f *o, float deg);
void mat4f_translate(Mat4f *o, float x, float y, float z);
void mat4f_transform(const Mat4f *m, const Vec3f *in, Vec3f *out);
void mat4f_transform_dir(const Mat4f *m, const Vec3f *in, Vec3f *out);

void math3d_cam_set(float x, float y, float z, float yaw, float pitch);
void math3d_cam_reset(void);
void math3d_cam_get(Vec3f *pos, float *yaw, float *pitch);
void math3d_view(Mat4f *o);

uint32_t depth_to_z(float clip_z);

void math3d_set_screen(int width, int height);
int math3d_screen_w(void);
int math3d_screen_h(void);

int project_view(float x, float y, float z, int *sx, int *sy, uint32_t *sz);
int project_vertex(const Mat4f *mvp, float x, float y, float z,
                   int *sx, int *sy, uint32_t *sz);

#endif
