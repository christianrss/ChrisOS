#ifndef CHRIS_VIRGL_CMD_H
#define CHRIS_VIRGL_CMD_H

#include <stdint.h>

#define VIRGL_CMD_MAX 1024

typedef struct VirglCmd {
    uint32_t *d;
    uint32_t cap;
    uint32_t n;
    uint32_t expect;
    int err;
    uint32_t ctx;
} VirglCmd;

int virgl_cmd_init(VirglCmd *c, uint32_t *buf, uint32_t cap_dwords, uint32_t ctx);
int virgl_cmd_ok(const VirglCmd *c);
uint32_t virgl_cmd_len(const VirglCmd *c);
int virgl_cmd_u32(VirglCmd *c, uint32_t v);
int virgl_cmd_begin(VirglCmd *c, uint32_t cmd, uint32_t obj, uint32_t len);
int virgl_cmd_end(VirglCmd *c);
int virgl_cmd_clear(VirglCmd *c, uint32_t buffers, uint32_t c0, uint32_t c1,
                    uint32_t c2, uint32_t c3, uint64_t depth_bits,
                    uint32_t stencil);
int virgl_cmd_surface(VirglCmd *c, uint32_t handle, uint32_t res, uint32_t fmt);
int virgl_cmd_framebuffer(VirglCmd *c, uint32_t nr, uint32_t zs, uint32_t color);
int virgl_cmd_viewport(VirglCmd *c, uint32_t sx, uint32_t sy, uint32_t sz,
                       uint32_t tx, uint32_t ty, uint32_t tz);
int virgl_cmd_bind(VirglCmd *c, uint32_t obj, uint32_t handle);
int virgl_cmd_blend_opaque(VirglCmd *c, uint32_t handle);
int virgl_cmd_dsa(VirglCmd *c, uint32_t handle, int depth_enable, uint32_t func);
int virgl_cmd_raster(VirglCmd *c, uint32_t handle, uint32_t cull);
int virgl_cmd_velems(VirglCmd *c, uint32_t handle, uint32_t n,
                     const uint32_t *offset, const uint32_t *fmt);
int virgl_cmd_vbuffers(VirglCmd *c, uint32_t stride, uint32_t offset,
                       uint32_t res);
int virgl_cmd_ib(VirglCmd *c, uint32_t res, uint32_t index_size);
int virgl_cmd_draw(VirglCmd *c, uint32_t start, uint32_t count, int indexed,
                   uint32_t min_index, uint32_t max_index);
int virgl_cmd_shader(VirglCmd *c, uint32_t handle, uint32_t stage,
                     const char *text);
int virgl_cmd_link(VirglCmd *c, uint32_t vs, uint32_t fs);
int virgl_cmd_consts(VirglCmd *c, uint32_t stage, const uint32_t *words,
                     uint32_t nwords);
int virgl_cmd_sampler(VirglCmd *c, uint32_t handle);
int virgl_cmd_sview(VirglCmd *c, uint32_t handle, uint32_t res, uint32_t fmt);
int virgl_cmd_bind_sampler(VirglCmd *c, uint32_t stage, uint32_t handle);
int virgl_cmd_set_views(VirglCmd *c, uint32_t stage, uint32_t handle);

#endif
