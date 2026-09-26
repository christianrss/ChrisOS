#ifndef CHRIS_SH_PUB_H
#define CHRIS_SH_PUB_H

#include <stdint.h>

/* ChrisOS GLSL subset. This is not a GLSL 3.30 implementation. */

#define SH_STAGE_VERTEX 0
#define SH_STAGE_FRAGMENT 1
#define SH_STAGE_GEOMETRY 2
#define SH_STAGE_COMPUTE 3

#define SH_TY_NONE 0
#define SH_TY_VOID 1
#define SH_TY_BOOL 2
#define SH_TY_INT 3
#define SH_TY_FLOAT 4
#define SH_TY_VEC2 5
#define SH_TY_VEC3 6
#define SH_TY_VEC4 7
#define SH_TY_IVEC2 8
#define SH_TY_IVEC3 9
#define SH_TY_IVEC4 10
#define SH_TY_MAT3 11
#define SH_TY_MAT4 12
#define SH_TY_SAMPLER2D 13

#define SH_SRC_MAX 4096
#define SH_COMPILER_VERSION 1

#define SH_STAT_LEX 0
#define SH_STAT_PARSE 1
#define SH_STAT_SEM 2
#define SH_STAT_IR 3
#define SH_STAT_TGSI 4

typedef struct ShShader ShShader;
typedef struct ShProgram ShProgram;

typedef struct ShUniformInfo {
    char name[40];
    int type;
    int stage;
    int slot;
    int offset;
    int size;
} ShUniformInfo;

typedef struct ShAttribInfo {
    char name[40];
    int type;
    int location;
    int ncomp;
} ShAttribInfo;

typedef struct ShVaryInfo {
    char name[40];
    int type;
    int slot;
    int stage;
} ShVaryInfo;

ShShader *sh_compile(int stage, const char *name, const char *src);
void sh_shader_free(ShShader *s);
int sh_shader_ok(const ShShader *s);
int sh_shader_stage(const ShShader *s);
const char *sh_shader_log(const ShShader *s);
const char *sh_shader_tgsi(const ShShader *s);
const char *sh_shader_ir(const ShShader *s);
const char *sh_shader_ast(const ShShader *s);
uint64_t sh_shader_cycles(const ShShader *s, int stat);

ShProgram *sh_program_create(void);
void sh_program_free(ShProgram *p);
int sh_program_attach(ShProgram *p, ShShader *s);
int sh_program_link(ShProgram *p);
int sh_program_ok(const ShProgram *p);
uint32_t sh_program_gen(const ShProgram *p);
ShProgram *sh_guest_program(int owner, int id);
const char *sh_program_log(const ShProgram *p);
const char *sh_program_tgsi(const ShProgram *p, int stage);

int sh_uniform_count(const ShProgram *p);
int sh_uniform_info(const ShProgram *p, int index, ShUniformInfo *out);
int sh_uniform_find(const ShProgram *p, const char *name);
int sh_uniform_set(ShProgram *p, int loc, const float *v, int nfloat);
int sh_uniform_set_lane(ShProgram *p, int loc, int lane, float v);

int sh_attrib_count(const ShProgram *p);
int sh_attrib_info(const ShProgram *p, int index, ShAttribInfo *out);
int sh_vary_count(const ShProgram *p);
int sh_vary_info(const ShProgram *p, int index, ShVaryInfo *out);
int sh_sampler_count(const ShProgram *p);
int sh_sampler_find(const ShProgram *p, const char *name);

const float *sh_uniform_words(const ShProgram *p, int stage, int *nvec);

int sh_live_count(void);
int sh_cache_count(void);

int sh_soft_vs(const ShProgram *p, const float *attr, float pos[4], float vary[][4]);
int sh_soft_fs(const ShProgram *p, const float vary_in[][4], const float fragcoord[4],
               const uint8_t *tex, int tex_w, int tex_h, float color[4], int *discarded);
int sh_soft_triangle(const ShProgram *p, const float *v0, const float *v1, const float *v2,
                     int attr_stride, const uint8_t *tex, int tex_w, int tex_h,
                     uint32_t *pixels, int w, int h);

int sh_shader_save_csi(const ShShader *s, void *dst, int cap);
ShShader *sh_shader_load_csi(const void *src, int len);

int sh_guest_compile(int owner, int stage, const char *name, const char *src);
int sh_guest_shader_ok(int owner, int id);
int sh_guest_shader_log(int owner, int id, char *dst, int cap);
int sh_guest_shader_drop(int owner, int id);
int sh_guest_prog_make(int owner);
int sh_guest_prog_attach(int owner, int prog, int shader);
int sh_guest_prog_link(int owner, int prog);
int sh_guest_prog_ok(int owner, int prog);
int sh_guest_prog_log(int owner, int prog, char *dst, int cap);
int sh_guest_prog_drop(int owner, int prog);
int sh_guest_uniloc(int owner, int prog, const char *name);
int sh_guest_setf(int owner, int prog, int loc, int lane, float v);
int sh_guest_samp(int owner, int prog, const char *name);
void sh_guest_drop_owner(int owner);

#endif
