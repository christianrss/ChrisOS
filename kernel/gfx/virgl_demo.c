#include "virgl_demo.h"

#include "bootinfo.h"
#include "math3d.h"
#include "serial.h"
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
static uint32_t g_vs;
static uint32_t g_fs;
static uint32_t g_vs_mvp;
static uint32_t g_vs_col;
static uint32_t g_fs_tex;
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

static int pipe_init(void) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    static const char vs[] =
        "VERT\n"
        "DCL IN[0]\n"
        "DCL IN[1]\n"
        "DCL OUT[0], POSITION\n"
        "DCL OUT[1], COLOR\n"
        "  0: MOV OUT[0], IN[0]\n"
        "  1: MOV OUT[1], IN[1]\n"
        "  2: END\n";
    static const char fs[] =
        "FRAG\n"
        "DCL IN[0], COLOR, LINEAR\n"
        "DCL OUT[0], COLOR\n"
        "  0: MOV OUT[0], IN[0]\n"
        "  1: END\n";
    static const char vs_col[] =
        "VERT\n"
        "DCL IN[0]\n"
        "DCL IN[1]\n"
        "DCL OUT[0], POSITION\n"
        "DCL OUT[1], COLOR\n"
        "DCL CONST[0][0..3]\n"
        "DCL TEMP[0]\n"
        "  0: MUL TEMP[0], IN[0].xxxx, CONST[0][0]\n"
        "  1: MAD TEMP[0], IN[0].yyyy, CONST[0][1], TEMP[0]\n"
        "  2: MAD TEMP[0], IN[0].zzzz, CONST[0][2], TEMP[0]\n"
        "  3: MAD OUT[0], IN[0].wwww, CONST[0][3], TEMP[0]\n"
        "  4: MOV OUT[1], IN[1]\n"
        "  5: END\n";
    static const char vs_mvp[] =
        "VERT\n"
        "DCL IN[0]\n"
        "DCL IN[1]\n"
        "DCL OUT[0], POSITION\n"
        "DCL OUT[1], GENERIC[0]\n"
        "DCL CONST[0][0..3]\n"
        "DCL TEMP[0]\n"
        "  0: MUL TEMP[0], IN[0].xxxx, CONST[0][0]\n"
        "  1: MAD TEMP[0], IN[0].yyyy, CONST[0][1], TEMP[0]\n"
        "  2: MAD TEMP[0], IN[0].zzzz, CONST[0][2], TEMP[0]\n"
        "  3: MAD OUT[0], IN[0].wwww, CONST[0][3], TEMP[0]\n"
        "  4: MOV OUT[1], IN[1]\n"
        "  5: END\n";
    static const char fs_tex[] =
        "FRAG\n"
        "DCL IN[0], GENERIC[0], PERSPECTIVE\n"
        "DCL OUT[0], COLOR\n"
        "DCL SAMP[0]\n"
        "DCL SVIEW[0], 2D, FLOAT\n"
        "  0: TEX OUT[0], IN[0], SAMP[0], 2D\n"
        "  1: END\n";
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
    g_vs = obj();
    g_fs = obj();
    g_vs_mvp = obj();
    g_vs_col = obj();
    g_fs_tex = obj();
    g_samp = obj();
    if (virgl_cmd_blend_opaque(&c, g_blend) != 0 ||
        virgl_cmd_dsa(&c, g_dsa, 1, 1u) != 0 ||
        virgl_cmd_dsa(&c, g_dsa_off, 0, 7u) != 0 ||
        virgl_cmd_raster(&c, g_rs, 0u) != 0 ||
        virgl_cmd_raster(&c, g_rs_cull, 2u) != 0 ||
        virgl_cmd_shader(&c, g_vs, VIRGL_SHADER_VERTEX, vs) != 0 ||
        virgl_cmd_shader(&c, g_fs, VIRGL_SHADER_FRAGMENT, fs) != 0 ||
        virgl_cmd_shader(&c, g_vs_mvp, VIRGL_SHADER_VERTEX, vs_mvp) != 0 ||
        virgl_cmd_shader(&c, g_vs_col, VIRGL_SHADER_VERTEX, vs_col) != 0 ||
        virgl_cmd_shader(&c, g_fs_tex, VIRGL_SHADER_FRAGMENT, fs_tex) != 0 ||
        virgl_cmd_sampler(&c, g_samp) != 0) {
        return -1;
    }
    if (!virgl_cmd_ok(&c) || vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c)) != 0) {
        serial_puts("FAIL: virgl pipeline\n");
        return -1;
    }
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

static int bind_color_pipe(VirglCmd *c, int depth) {
    if (virgl_cmd_bind(c, VIRGL_OBJECT_BLEND, g_blend) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_DSA, depth ? g_dsa : g_dsa_off) != 0 ||
        virgl_cmd_bind(c, VIRGL_OBJECT_RASTERIZER, g_rs) != 0 ||
        virgl_cmd_link(c, g_vs, g_fs) != 0) {
        return -1;
    }
    return 0;
}

static int draw_arrays(const float *verts, uint32_t nbytes, uint32_t stride, uint32_t count,
                       int depth) {
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
        bind_color_pipe(&c, depth) != 0 || virgl_cmd_velems(&c, ve, 2, off, fmt) != 0 ||
        virgl_cmd_bind(&c, VIRGL_OBJECT_VERTEX_ELEMENTS, ve) != 0 ||
        virgl_cmd_vbuffers(&c, stride, 0, vbo) != 0 ||
        virgl_cmd_draw(&c, 0, count, 0, 0, count) != 0) {
        return -1;
    }
    return vgpu_submit3d(g_ctx, buf, virgl_cmd_len(&c));
}

static void mvp_words(uint32_t w[16], float deg) {
    float c = gfx_cosf(deg);
    float s = gfx_sinf(deg);
    float sx = 1.1f;
    float sy = 1.1f;
    w[0] = fbits(sx * c);
    w[1] = 0;
    w[2] = fbits(-0.5f * s);
    w[3] = fbits(s);
    w[4] = 0;
    w[5] = fbits(sy);
    w[6] = 0;
    w[7] = 0;
    w[8] = fbits(sx * s);
    w[9] = 0;
    w[10] = fbits(0.5f * c);
    w[11] = fbits(-c);
    w[12] = 0;
    w[13] = 0;
    w[14] = fbits(-0.5f);
    w[15] = fbits(3.0f);
}

static int draw_indexed(const float *verts, uint32_t vbytes, const uint16_t *idx,
                        uint32_t ibytes, uint32_t count, int textured, uint32_t tex,
                        int cull) {
    uint32_t buf[VIRGL_CMD_MAX];
    VirglCmd c;
    uint32_t vbo = 0;
    uint32_t ib = 0;
    uint32_t ve = obj();
    uint32_t words[16];
    uint32_t off[2];
    uint32_t fmt[2];
    off[0] = 0;
    fmt[0] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    if (textured) {
        off[1] = 16;
        fmt[1] = VIRGL_FORMAT_R32G32_FLOAT;
    } else {
        off[1] = 16;
        fmt[1] = VIRGL_FORMAT_R32G32B32A32_FLOAT;
    }
    if (upload_buffer(VIRGL_BIND_VERTEX_BUFFER, verts, vbytes, &vbo) != 0 ||
        upload_buffer(VIRGL_BIND_INDEX_BUFFER, idx, ibytes, &ib) != 0) {
        return -1;
    }
    mvp_words(words, 28.0f);
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
            virgl_cmd_link(&c, g_vs_mvp, g_fs_tex) != 0 ||
            virgl_cmd_consts(&c, VIRGL_SHADER_VERTEX, words, 16) != 0 ||
            virgl_cmd_bind_sampler(&c, VIRGL_SHADER_FRAGMENT, g_samp) != 0 ||
            virgl_cmd_set_views(&c, VIRGL_SHADER_FRAGMENT, g_view) != 0) {
            return -1;
        }
    } else if (virgl_cmd_link(&c, g_vs_col, g_fs) != 0 ||
               virgl_cmd_consts(&c, VIRGL_SHADER_VERTEX, words, 16) != 0) {
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
    if (draw_arrays(v, sizeof v, 32u, 3u, 0) != 0 || readback() != 0) {
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
        draw_arrays(v, sizeof v, 32u, 6u, 1) != 0 || readback() != 0) {
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

static void cleanup(void) {
    int i;
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
        test_cube(0) != 0 || test_cube(1) != 0 || present_scanout() != 0) {
        cleanup();
        return -1;
    }
    cleanup();
    return 0;
}
