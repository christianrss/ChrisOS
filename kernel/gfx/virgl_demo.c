#include "virgl_demo.h"

#include "bootinfo.h"
#include "math3d.h"
#include "serial.h"
#include "shader/sh_pub.h"
#include "shader/sh_src.h"
#include "vgpu.h"
#include "virgl_cmd.h"
#include "virgl_proto.h"

#define DEMO_W 128u
#define DEMO_H 128u

static uint32_t g_obj = 0x1000u;
static uint32_t g_ctx;
static uint32_t g_color;
static uint32_t g_depth;
static int g_color_dma = -1;
static int g_depth_dma = -1;
static uint32_t g_blend;
static uint32_t g_dsa;
static uint32_t g_dsa_off;
static uint32_t g_rs;
static uint32_t g_rs_cull;
static uint32_t g_h_tri_vs;
static uint32_t g_h_tri_fs;
static uint32_t g_h_mvp_vs;
static uint32_t g_h_mvp_fs;
static uint32_t g_h_tex_vs;
static uint32_t g_h_tex_fs;
static uint32_t g_h_vary_vs;
static uint32_t g_h_vary_fs;
static uint32_t g_h_light_vs;
static uint32_t g_h_light_fs;
static uint32_t g_h_world_vs;
static uint32_t g_h_world_fs;
static ShProgram *g_prog_tri;
static ShProgram *g_prog_mvp;
static ShProgram *g_prog_tex;
static ShProgram *g_prog_vary;
static ShProgram *g_prog_light;
static ShProgram *g_prog_world;
static ShShader *g_sh_keep[16];
static int g_nsh;
static uint32_t g_trash[24];
static int g_ntrash;
static uint32_t g_samp;
static uint32_t g_view;
static int g_pipe;

static uint32_t obj(void) {
    return g_obj++;
}

static uint32_t fbits(float f) {
    union {
        float f;
        uint32_t u;
    } u;
    u.f = f;
    return u.u;
}

static int ch_hi(uint32_t p, int shift) {
    return ((p >> shift) & 255u) >= 170u;
}

static int ch_lo(uint32_t p, int shift) {
    return ((p >> shift) & 255u) <= 60u;
}

static int pix_red(uint32_t p) {
    return ch_hi(p, 16) && ch_lo(p, 8) && ch_lo(p, 0) && ch_hi(p, 24);
}

static int pix_green(uint32_t p) {
    return ch_lo(p, 16) && ch_hi(p, 8) && ch_lo(p, 0) && ch_hi(p, 24);
}

static int pix_blue(uint32_t p) {
    return ch_lo(p, 16) && ch_lo(p, 8) && ch_hi(p, 0) && ch_hi(p, 24);
}

static const uint32_t *pixels(void) {
    return (const uint32_t *)vgpu_dma_ptr(g_color_dma);
}

static int readback(void) {
    VgpuXfer3D box;
    box.resource_id = g_color;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = DEMO_W;
    box.h = DEMO_H;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = DEMO_W * 4u;
    box.layer_stride = DEMO_W * DEMO_H * 4u;
    return vgpu_xfer3d(0, g_ctx, &box);
}

static int count_if(int (*pred)(uint32_t)) {
    uint32_t i;
    int n = 0;
    const uint32_t *p = pixels();
    if (!p) {
        return -1;
    }
    for (i = 0; i < DEMO_W * DEMO_H; ++i) {
        if (pred(p[i])) {
            n++;
        }
    }
    return n;
}

static uint32_t pick_capset(void) {
    uint32_t i;
    uint32_t fallback = 0;
    for (i = 0; i < vgpu_capset_n(); ++i) {
        uint32_t id = 0;
        if (vgpu_capset_at(i, &id, 0, 0, 0) != 0) {
            continue;
        }
        if (id == VGPU_CAPSET_VIRGL2) {
            return id;
        }
        if (id == VGPU_CAPSET_VIRGL) {
            fallback = id;
        }
    }
    return fallback;
}

static int ctx_cycle(uint32_t capset, int n) {
    int i;
    int before = vgpu_ctx_live_count();
    for (i = 0; i < n; ++i) {
        uint32_t ctx = 0;
        if (vgpu_ctx_create(VGPU_OWNER_KERNEL, capset, 1, &ctx) != 0 ||
            vgpu_ctx_destroy(VGPU_OWNER_KERNEL, ctx) != 0) {
            serial_puts("FAIL: virgl ctx cycle\n");
            return -1;
        }
    }
    if (vgpu_ctx_live_count() != before) {
        serial_puts("FAIL: virgl ctx leak\n");
        return -1;
    }
    return 0;
}

static int res3d_cycle(int n) {
    int i;
    int before = vgpu_res_live_count();
    VgpuCreate3D info;
    info.resource_id = 0;
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
    info.bind = VIRGL_BIND_RENDER_TARGET;
    info.width = 1;
    info.height = 1;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 1u;
    for (i = 0; i < n; ++i) {
        uint32_t id = 0;
        if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, -1, 0, &id) != 0 ||
            vgpu_ctx_attach(g_ctx, id) != 0 || vgpu_ctx_detach(g_ctx, id) != 0 ||
            vgpu_res_unref(VGPU_OWNER_KERNEL, id) != 0) {
            serial_puts("FAIL: virgl resource cycle\n");
            return -1;
        }
    }
    if (vgpu_res_live_count() != before) {
        serial_puts("FAIL: virgl resource leak\n");
        return -1;
    }
    serial_puts("PASS: virgl resource stress ");
    serial_write_u64((uint64_t)n);
    serial_puts("\n");
    return 0;
}

static int make_color(void) {
    VgpuCreate3D info;
    uint32_t bytes = DEMO_W * DEMO_H * 4u;
    int pages = (int)((bytes + 4095u) / 4096u);
    g_color_dma = vgpu_alloc_dma(pages);
    if (g_color_dma < 0) {
        return -1;
    }
    info.resource_id = 0;
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
    info.bind = VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SCANOUT | VIRGL_BIND_DISPLAY_TARGET;
    info.width = DEMO_W;
    info.height = DEMO_H;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 1u;
    if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, g_color_dma, bytes, &g_color) != 0) {
        return -1;
    }
    return vgpu_ctx_attach(g_ctx, g_color);
}

static int make_depth(void) {
    VgpuCreate3D info;
    info.resource_id = 0;
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_Z32_FLOAT;
    info.bind = VIRGL_BIND_DEPTH_STENCIL;
    info.width = DEMO_W;
    info.height = DEMO_H;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 0;
    if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, -1, 0, &g_depth) != 0) {
        return -1;
    }
    return vgpu_ctx_attach(g_ctx, g_depth);
}

static int upload_buffer(uint32_t bind, const void *src, uint32_t bytes, uint32_t *id) {
    VgpuCreate3D info;
    VgpuXfer3D box;
    int dma = vgpu_alloc_dma(1);
    uint8_t *dst;
    uint32_t i;
    if (dma < 0 || bytes > 4096u) {
        return -1;
    }
    dst = vgpu_dma_ptr(dma);
    for (i = 0; i < bytes; ++i) {
        dst[i] = ((const uint8_t *)src)[i];
    }
    info.resource_id = 0;
    info.target = VIRGL_TARGET_BUFFER;
    info.format = VIRGL_FORMAT_R8_UNORM;
    info.bind = bind;
    info.width = bytes;
    info.height = 1;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 0;
    if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, dma, bytes, id) != 0 ||
        vgpu_ctx_attach(g_ctx, *id) != 0) {
        return -1;
    }
    if (g_ntrash < 24) {
        g_trash[g_ntrash++] = *id;
    }
    box.resource_id = *id;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = bytes;
    box.h = 1;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = 0;
    box.layer_stride = 0;
    return vgpu_xfer3d(1, g_ctx, &box);
}

static int upload_tex(uint32_t *id) {
    VgpuCreate3D info;
    VgpuXfer3D box;
    int dma = vgpu_alloc_dma(1);
    uint32_t *dst;
    uint32_t i;
    if (dma < 0) {
        return -1;
    }
    dst = (uint32_t *)vgpu_dma_ptr(dma);
    for (i = 0; i < 16u; ++i) {
        dst[i] = ((i / 2u) & 1u) ? 0xFF0000FFu : 0xFFFF0000u;
    }
    info.resource_id = 0;
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
    info.bind = VIRGL_BIND_SAMPLER_VIEW;
    info.width = 4;
    info.height = 4;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 1u;
    if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, dma, 64u, id) != 0 ||
        vgpu_ctx_attach(g_ctx, *id) != 0) {
        return -1;
    }
    if (g_ntrash < 24) {
        g_trash[g_ntrash++] = *id;
    }
    box.resource_id = *id;
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = 4;
    box.h = 4;
    box.d = 1;
    box.offset = 0;
    box.level = 0;
    box.stride = 16;
    box.layer_stride = 64;
    return vgpu_xfer3d(1, g_ctx, &box);
}

static int begin_cmd(VirglCmd *c, uint32_t *buf) {
    return virgl_cmd_init(c, buf, VIRGL_CMD_MAX, g_ctx);
}

static int emit_fb(VirglCmd *c, int with_depth) {
    uint32_t surf = obj();
    uint32_t zs = 0;
    if (virgl_cmd_surface(c, surf, g_color, VIRGL_FORMAT_B8G8R8A8_UNORM) != 0) {
        return -1;
    }
    if (with_depth) {
        zs = obj();
        if (virgl_cmd_surface(c, zs, g_depth, VIRGL_FORMAT_Z32_FLOAT) != 0) {
            return -1;
        }
    }
    return virgl_cmd_framebuffer(c, 1u, zs, surf);
}

static int emit_view(VirglCmd *c) {
    return virgl_cmd_viewport(c, fbits((float)DEMO_W * 0.5f), fbits((float)DEMO_H * 0.5f),
                              VIRGL_F32_HALF, fbits((float)DEMO_W * 0.5f),
                              fbits((float)DEMO_H * 0.5f), VIRGL_F32_HALF);
}

static int text_len(const char *s) {
    int n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        ++n;
    }
    return n;
}

static int keep_shader(ShShader *s) {
    if (!s || g_nsh >= 16) {
        return -1;
    }
    g_sh_keep[g_nsh++] = s;
    return 0;
}

static int submit_shader(uint32_t handle, uint32_t stage, const char *text) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    if (text_len(text) <= 0 || text_len(text) > 3500) {
        serial_puts("FAIL: glsl tgsi size\n");
        return -1;
    }
    if (begin_cmd(&c, buf) != 0 || virgl_cmd_shader(&c, handle, stage, text) != 0 ||
        !virgl_cmd_ok(&c) || vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c)) != 0) {
        serial_puts("FAIL: glsl shader submit\n");
        return -1;
    }
    return 0;
}

static int build_prog(const char *vn, const char *vs, const char *fn, const char *fs,
                      ShProgram **out, uint32_t *hvs, uint32_t *hfs) {
    ShShader *a = sh_compile(SH_STAGE_VERTEX, vn, vs);
    ShShader *b = sh_compile(SH_STAGE_FRAGMENT, fn, fs);
    ShProgram *p = sh_program_create();
    if (!a || !b || !p || keep_shader(a) != 0 || keep_shader(b) != 0) {
        serial_puts("FAIL: glsl alloc\n");
        return -1;
    }
    if (!sh_shader_ok(a)) {
        serial_puts(sh_shader_log(a));
        serial_puts("FAIL: glsl vertex\n");
        return -1;
    }
    if (!sh_shader_ok(b)) {
        serial_puts(sh_shader_log(b));
        serial_puts("FAIL: glsl fragment\n");
        return -1;
    }
    if (sh_program_attach(p, a) != 0 || sh_program_attach(p, b) != 0 || sh_program_link(p) != 0 ||
        !sh_program_ok(p)) {
        serial_puts(sh_program_log(p));
        serial_puts("FAIL: glsl link\n");
        return -1;
    }
    *hvs = obj();
    *hfs = obj();
    if (submit_shader(*hvs, VIRGL_SHADER_VERTEX, sh_program_tgsi(p, SH_STAGE_VERTEX)) != 0 ||
        submit_shader(*hfs, VIRGL_SHADER_FRAGMENT, sh_program_tgsi(p, SH_STAGE_FRAGMENT)) != 0) {
        return -1;
    }
    *out = p;
    return 0;
}

static void ident4(float m[16]) {
    int i;
    for (i = 0; i < 16; ++i) {
        m[i] = 0.f;
    }
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void model_from_deg(float m[16], float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    float sx = 1.1f;
    float sy = 1.1f;
    int i;
    for (i = 0; i < 16; ++i) {
        m[i] = 0.f;
    }
    m[0] = sx * c;
    m[2] = -0.5f * s;
    m[3] = s;
    m[5] = sy;
    m[8] = sx * s;
    m[10] = 0.5f * c;
    m[11] = -c;
    m[14] = -0.5f;
    m[15] = 3.0f;
}

static int set_mvp(ShProgram *p, float deg) {
    float model[16];
    float view[16];
    float proj[16];
    model_from_deg(model, deg);
    ident4(view);
    ident4(proj);
    if (sh_uniform_set(p, sh_uniform_find(p, "model"), model, 16) != 0 ||
        sh_uniform_set(p, sh_uniform_find(p, "view"), view, 16) != 0 ||
        sh_uniform_set(p, sh_uniform_find(p, "projection"), proj, 16) != 0) {
        return -1;
    }
    return 0;
}

static int upload_uni(VirglCmd *c, ShProgram *p, int stage) {
    const float *w;
    int nvec = 0;
    uint32_t bits[64];
    int i;
    int n;
    uint32_t gst;
    if (!p) {
        return 0;
    }
    w = sh_uniform_words(p, stage, &nvec);
    if (nvec <= 0) {
        return 0;
    }
    n = nvec * 4;
    if (!w || n > 64) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        bits[i] = fbits(w[i]);
    }
    gst = stage == SH_STAGE_FRAGMENT ? VIRGL_SHADER_FRAGMENT : VIRGL_SHADER_VERTEX;
    return virgl_cmd_consts(c, gst, bits, (uint32_t)n);
}

static int pipe_init(void) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    g_h_world_vs = 0;
    g_h_world_fs = 0;
    if (g_pipe) {
        return 0;
    }
    if (begin_cmd(&c, buf) != 0) {
        return -1;
    }
    g_blend = obj();
    g_dsa = obj();
    g_dsa_off = obj();
    g_rs = obj();
    g_rs_cull = obj();
    g_samp = obj();
    if (virgl_cmd_blend_opaque(&c, g_blend) != 0 || virgl_cmd_dsa(&c, g_dsa, 1, 1u) != 0 ||
        virgl_cmd_dsa(&c, g_dsa_off, 0, 7u) != 0 || virgl_cmd_raster(&c, g_rs, 0u) != 0 ||
        virgl_cmd_raster(&c, g_rs_cull, 2u) != 0 || virgl_cmd_sampler(&c, g_samp) != 0) {
        return -1;
    }
    if (!virgl_cmd_ok(&c) || vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c)) != 0) {
        serial_puts("FAIL: virgl pipeline\n");
        return -1;
    }
    if (build_prog("tri.vert", SH_SRC_TRI_VERT, "tri.frag", SH_SRC_TRI_FRAG, &g_prog_tri,
                   &g_h_tri_vs, &g_h_tri_fs) != 0 ||
        build_prog("mvp.vert", SH_SRC_MVP_VERT, "tri.frag", SH_SRC_TRI_FRAG, &g_prog_mvp,
                   &g_h_mvp_vs, &g_h_mvp_fs) != 0 ||
        build_prog("tex.vert", SH_SRC_TEX_VERT, "tex.frag", SH_SRC_TEX_FRAG, &g_prog_tex,
                   &g_h_tex_vs, &g_h_tex_fs) != 0 ||
        build_prog("vary.vert", SH_SRC_VARY_VERT, "vary.frag", SH_SRC_VARY_FRAG, &g_prog_vary,
                   &g_h_vary_vs, &g_h_vary_fs) != 0 ||
        build_prog("light.vert", SH_SRC_LIGHT_VERT, "light.frag", SH_SRC_LIGHT_FRAG, &g_prog_light,
                   &g_h_light_vs, &g_h_light_fs) != 0 ||
        build_prog("world.vert", SH_SRC_WORLD_VERT, "world.frag", SH_SRC_WORLD_FRAG, &g_prog_world,
                   &g_h_world_vs, &g_h_world_fs) != 0) {
        return -1;
    }
    serial_puts("PASS: glsl compile\n");
    serial_puts("PASS: shader mine link\n");
    g_pipe = 1;
    return 0;
}

static int clear_color(uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3, int depth) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    if (begin_cmd(&c, buf) != 0 || emit_fb(&c, depth) != 0 || emit_view(&c) != 0) {
        return -1;
    }
    if (virgl_cmd_clear(&c, VIRGL_CLEAR_COLOR0 | (depth ? 1u : 0u), c0, c1, c2, c3,
                        0x3ff0000000000000ull, 0) != 0 ||
        !virgl_cmd_ok(&c)) {
        return -1;
    }
    return vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c));
}

static int bind_color_pipe(VirglCmd *c, int depth, uint32_t vs, uint32_t fs) {
    if (virgl_cmd_bind(c, VIRGL_OBJECT_BLEND, g_blend) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_DSA, depth ? g_dsa : g_dsa_off) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_RASTERIZER, g_rs) != 0 || virgl_cmd_link(c, vs, fs) != 0) {
        return -1;
    }
    return 0;
}

static int draw_arrays(const float *verts, uint32_t nbytes, uint32_t stride, uint32_t count,
                       int depth, uint32_t vs, uint32_t fs, ShProgram *prog) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    uint32_t vbo = 0;
    uint32_t ve = obj();
    uint32_t off[2];
    uint32_t fmt[2];
    off[0] = 0;
    off[1] = 16;
    fmt[0] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    fmt[1] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    if (upload_buffer(VIRGL_BIND_VERTEX_BUFFER, verts, nbytes, &vbo) != 0) {
        return -1;
    }
    if (begin_cmd(&c, buf) != 0 || emit_fb(&c, depth) != 0 || emit_view(&c) != 0 ||
        bind_color_pipe(&c, depth, vs, fs) != 0 || upload_uni(&c, prog, SH_STAGE_VERTEX) != 0 ||
        upload_uni(&c, prog, SH_STAGE_FRAGMENT) != 0 || virgl_cmd_velems(&c, ve, 2, off, fmt) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_VERTEX_ELEMENTS, ve) != 0 ||
        virgl_cmd_vbuffers(&c, stride, 0, vbo) != 0 ||
        virgl_cmd_draw(&c, 0, count, 0, 0, count) != 0) {
        return -1;
    }
    return vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c));
}

static int draw_indexed(const float *verts, uint32_t vbytes, const uint16_t *idx,
                        uint32_t ibytes, uint32_t count, int textured, uint32_t tex,
                        int cull) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    uint32_t vbo = 0;
    uint32_t ib = 0;
    uint32_t ve = obj();
    uint32_t off[2];
    uint32_t fmt[2];
    ShProgram *prog = textured ? g_prog_tex : g_prog_mvp;
    uint32_t hvs = textured ? g_h_tex_vs : g_h_mvp_vs;
    uint32_t hfs = textured ? g_h_tex_fs : g_h_mvp_fs;
    off[0] = 0;
    fmt[0] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    if (textured) {
        off[1] = 16;
        fmt[1] = VIRGL_FORMAT_R32G32_FLOAT;
    } else {
        off[1] = 16;
        fmt[1] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    }
    if (set_mvp(prog, 28.0f) != 0) {
        return -1;
    }
    if (upload_buffer(VIRGL_BIND_VERTEX_BUFFER, verts, vbytes, &vbo) != 0 ||
        upload_buffer(VIRGL_BIND_INDEX_BUFFER, idx, ibytes, &ib) != 0) {
        return -1;
    }
    if (begin_cmd(&c, buf) != 0 || emit_fb(&c, 1) != 0 || emit_view(&c) != 0) {
        return -1;
    }
    if (virgl_cmd_bind(&c, VIRGL_OBJECT_BLEND, g_blend) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_DSA, g_dsa) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_RASTERIZER, cull ? g_rs_cull : g_rs) != 0) {
        return -1;
    }
    if (textured) {
        if (g_view == 0u) {
            g_view = obj();
        }
        if (virgl_cmd_sview(&c, g_view, tex, VIRGL_FORMAT_B8G8R8A8_UNORM) != 0 ||
            virgl_cmd_link(&c, hvs, hfs) != 0 || upload_uni(&c, prog, SH_STAGE_VERTEX) != 0 ||
            virgl_cmd_bind_sampler(&c, VIRGL_SHADER_FRAGMENT, g_samp) != 0 ||
            virgl_cmd_set_views(&c, VIRGL_SHADER_FRAGMENT, g_view) != 0) {
            return -1;
        }
    } else if (virgl_cmd_link(&c, hvs, hfs) != 0 || upload_uni(&c, prog, SH_STAGE_VERTEX) != 0) {
        return -1;
    }
    if (virgl_cmd_velems(&c, ve, 2, off, fmt) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_VERTEX_ELEMENTS, ve) != 0 ||
        virgl_cmd_vbuffers(&c, textured ? 24u : 32u, 0, vbo) != 0 ||
        virgl_cmd_ib(&c, ib, 2u) != 0 ||
        virgl_cmd_draw(&c, 0, count, 1, 0, 64u) != 0) {
        return -1;
    }
    return vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c));
}

static int test_clear(void) {
    int n;
    if (clear_color(0, VIRGL_F32_ONE, 0, VIRGL_F32_ONE, 0) != 0 || readback() != 0) {
        serial_puts("FAIL: virgl clear submit\n");
        return -1;
    }
    n = count_if(pix_green);
    if (n < (int)(DEMO_W * DEMO_H * 8u / 10u)) {
        serial_puts("FAIL: virgl clear pixels ");
        serial_write_u64((uint64_t)n);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl clear\n");
    return 0;
}

static int test_tri(void) {
    static const float v[] = {
        -0.7f, -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.7f,  -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f,  0.7f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    int red;
    int green;
    if (draw_arrays(v, sizeof v, 32u, 3u, 0, g_h_tri_vs, g_h_tri_fs, 0) != 0 || readback() != 0) {
        serial_puts("FAIL: virgl triangle submit\n");
        return -1;
    }
    red = count_if(pix_red);
    green = count_if(pix_green);
    if (red < 40 || green < 40) {
        serial_puts("FAIL: virgl triangle red=");
        serial_write_u64((uint64_t)red);
        serial_puts(" green=");
        serial_write_u64((uint64_t)green);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl triangle\n");
    return 0;
}

static int test_depth(void) {
    static const float v[] = {
        -0.55f, -0.55f, -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.55f,  -0.55f, -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f,   0.55f,  -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        -0.55f, -0.55f, 0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.55f,  -0.55f, 0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.0f,   0.55f,  0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
    };
    int red;
    int green;
    if (clear_color(0, 0, VIRGL_F32_ONE, VIRGL_F32_ONE, 1) != 0 ||
        draw_arrays(v, sizeof v, 32u, 6u, 1, g_h_tri_vs, g_h_tri_fs, 0) != 0 || readback() != 0) {
        serial_puts("FAIL: virgl depth submit\n");
        return -1;
    }
    red = count_if(pix_red);
    green = count_if(pix_green);
    if (red < 40 || green > red / 4) {
        serial_puts("FAIL: virgl depth red=");
        serial_write_u64((uint64_t)red);
        serial_puts(" green=");
        serial_write_u64((uint64_t)green);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl depth\n");
    return 0;
}

static int test_cube(int textured) {
    static const float cube[] = {
        -0.5f, -0.5f, 0.5f,  1, 0, 0, 1, 1, 0.5f, -0.5f, 0.5f,  1, 0, 0, 1, 1,
        0.5f,  0.5f,  0.5f,  1, 0, 0, 1, 1, -0.5f, 0.5f,  0.5f,  1, 0, 0, 1, 1,
        0.5f,  -0.5f, -0.5f, 1, 0, 1, 0, 1, 0.5f, 0.5f,  -0.5f, 1, 0, 1, 0, 1,
        -0.5f, 0.5f,  -0.5f, 1, 0, 1, 0, 1, -0.5f, -0.5f, -0.5f, 1, 0, 1, 0, 1,
    };
    static const float tcube[] = {
        -0.5f, -0.5f, 0.5f, 1, 0, 0, 0.5f, -0.5f, 0.5f, 1, 1, 0,
        0.5f,  0.5f,  0.5f, 1, 1, 1, -0.5f, 0.5f, 0.5f, 1, 0, 1,
        0.5f,  -0.5f, -0.5f, 1, 0, 0, 0.5f, 0.5f, -0.5f, 1, 1, 0,
        -0.5f, 0.5f, -0.5f, 1, 1, 1, -0.5f, -0.5f, -0.5f, 1, 0, 1,
    };
    static const uint16_t idx[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7,
                                   0, 4, 7, 0, 7, 3, 1, 5, 6, 1, 6, 2,
                                   3, 2, 6, 3, 6, 7, 0, 1, 5, 0, 5, 4};
    uint32_t tex = 0;
    if (textured) {
        int red;
        int blue;
        if (clear_color(0, 0, 0, VIRGL_F32_ONE, 1) != 0 || upload_tex(&tex) != 0 ||
            draw_indexed(tcube, sizeof tcube, idx, sizeof idx, 36u, 1, tex, 1) != 0 ||
            readback() != 0) {
            serial_puts("FAIL: virgl textured cube submit\n");
            return -1;
        }
        red = count_if(pix_red);
        blue = count_if(pix_blue);
        if (red < 15 || blue < 15) {
            serial_puts("FAIL: virgl textured cube red=");
            serial_write_u64((uint64_t)red);
            serial_puts(" blue=");
            serial_write_u64((uint64_t)blue);
            serial_puts("\n");
            return -1;
        }
        serial_puts("PASS: virgl textured cube\n");
        return 0;
    }
    if (clear_color(0, 0, 0, VIRGL_F32_ONE, 1) != 0 ||
        draw_indexed(cube, sizeof cube, idx, sizeof idx, 36u, 0, 0, 1) != 0 ||
        readback() != 0) {
        serial_puts("FAIL: virgl cube submit\n");
        return -1;
    }
    if (count_if(pix_red) < 20 && count_if(pix_green) < 20) {
        serial_puts("FAIL: virgl cube pixels\n");
        return -1;
    }
    serial_puts("PASS: virgl cube\n");
    return 0;
}

static int pix_dim(uint32_t p) {
    uint32_t r = (p >> 16) & 255u;
    uint32_t g = (p >> 8) & 255u;
    uint32_t b = p & 255u;
    return r >= 15u && r <= 90u && g >= 15u && g <= 90u && b >= 15u && b <= 90u;
}

static int test_varying(void) {
    static const float v[] = {
        -0.8f, -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.8f,  -0.7f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.0f,  0.7f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
    };
    int red;
    int green;
    int blue;
    if (clear_color(0, 0, 0, VIRGL_F32_ONE, 0) != 0 ||
        draw_arrays(v, sizeof v, 32u, 3u, 0, g_h_vary_vs, g_h_vary_fs, 0) != 0 ||
        readback() != 0) {
        serial_puts("FAIL: virgl varying submit\n");
        return -1;
    }
    red = count_if(pix_red);
    green = count_if(pix_green);
    blue = count_if(pix_blue);
    if (red < 5 || green < 5 || blue < 5) {
        serial_puts("FAIL: virgl varying\n");
        return -1;
    }
    serial_puts("PASS: virgl varying\n");
    return 0;
}

static int test_lighting(void) {
    static const float v[] = {
        -0.85f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.05f, -0.65f, 0.0f, 1.0f,
        0.0f,   0.0f,   1.0f, 0.0f, -0.45f, 0.70f, 0.0f, 1.0f, 0.0f, 0.0f,   1.0f, 0.0f,
        0.05f,  -0.65f, 0.0f, 1.0f, 0.0f,  0.0f,  -1.0f, 0.0f, 0.85f, -0.65f, 0.0f, 1.0f,
        0.0f,   0.0f,   -1.0f, 0.0f, 0.45f, 0.70f, 0.0f, 1.0f, 0.0f,  0.0f,   -1.0f, 0.0f,
    };
    float id[16];
    float light[3];
    float lcol[3];
    float amb[3];
    int bright;
    int dim;
    ident4(id);
    light[0] = 0.f;
    light[1] = 0.f;
    light[2] = 2.f;
    lcol[0] = 1.f;
    lcol[1] = 0.f;
    lcol[2] = 0.f;
    amb[0] = 0.12f;
    amb[1] = 0.12f;
    amb[2] = 0.12f;
    if (sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "model"), id, 16) != 0 ||
        sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "view"), id, 16) != 0 ||
        sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "projection"), id, 16) != 0 ||
        sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "lightPosition"), light, 3) != 0 ||
        sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "lightColor"), lcol, 3) != 0 ||
        sh_uniform_set(g_prog_light, sh_uniform_find(g_prog_light, "ambientColor"), amb, 3) != 0) {
        serial_puts("FAIL: virgl lighting uniforms\n");
        return -1;
    }
    if (clear_color(0, 0, 0, VIRGL_F32_ONE, 0) != 0 ||
        draw_arrays(v, sizeof v, 32u, 6u, 0, g_h_light_vs, g_h_light_fs, g_prog_light) != 0 ||
        readback() != 0) {
        serial_puts("FAIL: virgl lighting submit\n");
        return -1;
    }
    bright = count_if(pix_red);
    dim = count_if(pix_dim);
    if (bright < 15 || dim < 15) {
        serial_puts("FAIL: virgl lighting bright=");
        serial_write_u64((uint64_t)bright);
        serial_puts(" dim=");
        serial_write_u64((uint64_t)dim);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl lighting\n");
    return 0;
}

static int pix_dim1(uint32_t p) {
    uint32_t r = (p >> 16) & 255u;
    uint32_t g = (p >> 8) & 255u;
    uint32_t b = p & 255u;
    uint32_t hi = r;
    uint32_t lo;
    if (g > hi) {
        hi = g;
    }
    if (b > hi) {
        hi = b;
    }
    lo = r;
    if (g < lo) {
        lo = g;
    }
    if (b < lo) {
        lo = b;
    }
    return hi >= 15u && hi <= 80u && lo <= 12u;
}

static void rot_y(float m[16], float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    int i;
    for (i = 0; i < 16; ++i) {
        m[i] = 0.f;
    }
    m[0] = c;
    m[2] = -s;
    m[5] = 1.f;
    m[8] = s;
    m[10] = c;
    m[15] = 1.f;
}

static int test_lit_mesh(void) {
    static const float v[] = {
        -0.85f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,
        -0.05f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f,
        -0.45f, 0.70f,  0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        0.05f,  -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        0.85f,  -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        0.45f,  0.70f,  0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
    };
    float model[16];
    float view[16];
    float proj[16];
    float nm[9];
    float light[3];
    float lcol[3];
    float amb[3];
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    uint32_t vbo = 0;
    uint32_t tex = 0;
    uint32_t view_h;
    uint32_t ve;
    uint32_t off[3];
    uint32_t fmt[3];
    int red;
    int blue;
    int dim;
    rot_y(model, 28.0f);
    ident4(view);
    ident4(proj);
    nm[0] = model[0];
    nm[1] = model[1];
    nm[2] = model[2];
    nm[3] = model[4];
    nm[4] = model[5];
    nm[5] = model[6];
    nm[6] = model[8];
    nm[7] = model[9];
    nm[8] = model[10];
    light[0] = 0.f;
    light[1] = 0.f;
    light[2] = 2.f;
    lcol[0] = 1.f;
    lcol[1] = 1.f;
    lcol[2] = 1.f;
    amb[0] = 0.15f;
    amb[1] = 0.15f;
    amb[2] = 0.15f;
    if (sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "model"), model, 16) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "view"), view, 16) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "projection"), proj, 16) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "normalMatrix"), nm, 9) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "lightPosition"), light, 3) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "lightColor"), lcol, 3) != 0 ||
        sh_uniform_set(g_prog_world, sh_uniform_find(g_prog_world, "ambientColor"), amb, 3) != 0) {
        serial_puts("FAIL: virgl lit mesh uniforms\n");
        return -1;
    }
    off[0] = 0;
    off[1] = 16;
    off[2] = 32;
    fmt[0] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    fmt[1] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    fmt[2] = VIRGL_FORMAT_R32G32_FLOAT;
    ve = obj();
    view_h = obj();
    if (clear_color(0, 0, 0, VIRGL_F32_ONE, 0) != 0 || upload_tex(&tex) != 0 ||
        upload_buffer(VIRGL_BIND_VERTEX_BUFFER, v, sizeof v, &vbo) != 0) {
        serial_puts("FAIL: virgl lit mesh upload\n");
        return -1;
    }
    if (begin_cmd(&c, buf) != 0 || emit_fb(&c, 0) != 0 || emit_view(&c) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_BLEND, g_blend) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_DSA, g_dsa_off) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_RASTERIZER, g_rs) != 0 ||
        virgl_cmd_sview(&c, view_h, tex, VIRGL_FORMAT_B8G8R8A8_UNORM) != 0 ||
        virgl_cmd_link(&c, g_h_world_vs, g_h_world_fs) != 0 ||
        upload_uni(&c, g_prog_world, SH_STAGE_VERTEX) != 0 ||
        upload_uni(&c, g_prog_world, SH_STAGE_FRAGMENT) != 0 ||
        virgl_cmd_bind_sampler(&c, VIRGL_SHADER_FRAGMENT, g_samp) != 0 ||
        virgl_cmd_set_views(&c, VIRGL_SHADER_FRAGMENT, view_h) != 0 ||
        virgl_cmd_velems(&c, ve, 3, off, fmt) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_VERTEX_ELEMENTS, ve) != 0 ||
        virgl_cmd_vbuffers(&c, 40u, 0, vbo) != 0 ||
        virgl_cmd_draw(&c, 0, 6u, 0, 0, 6u) != 0 || !virgl_cmd_ok(&c) ||
        vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c)) != 0 || readback() != 0) {
        serial_puts("FAIL: virgl lit mesh submit\n");
        return -1;
    }
    red = count_if(pix_red);
    blue = count_if(pix_blue);
    dim = count_if(pix_dim1);
    if (red < 5 || blue < 5 || dim < 5) {
        serial_puts("FAIL: virgl lit mesh red=");
        serial_write_u64((uint64_t)red);
        serial_puts(" blue=");
        serial_write_u64((uint64_t)blue);
        serial_puts(" dim=");
        serial_write_u64((uint64_t)dim);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl lit mesh\n");
    return 0;
}

static int test_switch(void) {
    static const float v[] = {
        -0.7f, -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.7f,  -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f,  0.7f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    int red;
    if (clear_color(0, 0, VIRGL_F32_ONE, VIRGL_F32_ONE, 0) != 0 ||
        draw_arrays(v, sizeof v, 32u, 3u, 0, g_h_tri_vs, g_h_tri_fs, 0) != 0 || readback() != 0) {
        serial_puts("FAIL: virgl shader switch submit\n");
        return -1;
    }
    red = count_if(pix_red);
    if (red < 40) {
        serial_puts("FAIL: virgl shader switch\n");
        return -1;
    }
    serial_puts("PASS: virgl shader switch\n");
    return 0;
}

static int present_scanout(void) {
    uint32_t sw = 0;
    uint32_t sh = 0;
    uint32_t primary = vgpu_primary_res();
    vgpu_fb_size(&sw, &sh);
    if (vgpu_set_scanout(0, g_color, 0, 0, DEMO_W, DEMO_H) != 0 ||
        vgpu_res_flush(g_color, 0, 0, DEMO_W, DEMO_H) != 0) {
        serial_puts("FAIL: virgl present\n");
        return -1;
    }
    serial_puts("PASS: virgl present\n");
    if (primary != 0u && sw != 0u && sh != 0u) {
        (void)vgpu_set_scanout(0, primary, 0, 0, sw, sh);
    }
    return 0;
}

static void free_shaders(void) {
    int i;
    sh_program_free(g_prog_tri);
    sh_program_free(g_prog_mvp);
    sh_program_free(g_prog_tex);
    sh_program_free(g_prog_vary);
    sh_program_free(g_prog_light);
    sh_program_free(g_prog_world);
    g_prog_tri = 0;
    g_prog_mvp = 0;
    g_prog_tex = 0;
    g_prog_vary = 0;
    g_prog_light = 0;
    g_prog_world = 0;
    for (i = 0; i < g_nsh; ++i) {
        sh_shader_free(g_sh_keep[i]);
        g_sh_keep[i] = 0;
    }
    g_nsh = 0;
}

static void cleanup(void) {
    int i;
    free_shaders();
    if (g_ctx != 0u && g_color != 0u) {
        (void)vgpu_ctx_detach(g_ctx, g_color);
    }
    if (g_ctx != 0u && g_depth != 0u) {
        (void)vgpu_ctx_detach(g_ctx, g_depth);
    }
    for (i = 0; i < g_ntrash; ++i) {
        if (g_ctx != 0u) {
            (void)vgpu_ctx_detach(g_ctx, g_trash[i]);
        }
        (void)vgpu_res_drop(VGPU_OWNER_KERNEL, g_trash[i]);
    }
    g_ntrash = 0;
    if (g_color != 0u) {
        (void)vgpu_res_drop(VGPU_OWNER_KERNEL, g_color);
        g_color = 0;
        g_color_dma = -1;
    }
    if (g_depth != 0u) {
        (void)vgpu_res_drop(VGPU_OWNER_KERNEL, g_depth);
        g_depth = 0;
        g_depth_dma = -1;
    }
    if (g_ctx != 0u) {
        (void)vgpu_ctx_destroy(VGPU_OWNER_KERNEL, g_ctx);
        g_ctx = 0;
    }
}

int virgl_demo_run(void) {
    uint32_t cap = pick_capset();
    int cycles = bootflag_gfx_stress() ? 64 : 4;
    int res_cycles = bootflag_gfx_stress() ? 128 : 4;
    if (cap == 0u) {
        serial_puts("reason no VIRGL capset\n");
        return -1;
    }
    if (ctx_cycle(cap, cycles) != 0) {
        return -1;
    }
    if (vgpu_ctx_create(VGPU_OWNER_KERNEL, cap, 1, &g_ctx) != 0) {
        return -1;
    }
    if (res3d_cycle(res_cycles) != 0 || make_color() != 0 || make_depth() != 0 ||
        pipe_init() != 0 || test_clear() != 0 || test_tri() != 0 || test_depth() != 0 ||
        test_cube(0) != 0 || test_cube(1) != 0 || test_varying() != 0 || test_lighting() != 0 ||
        test_switch() != 0 || test_lit_mesh() != 0 || present_scanout() != 0) {
        cleanup();
        return -1;
    }
    cleanup();
    return 0;
}
