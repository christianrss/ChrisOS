#include "virgl_demo.h"

#include "bootinfo.h"
#include "gfx3d.h"
#include "math3d.h"
#include "serial.h"
#include "shader/sh_pub.h"
#include "shader/sh_src.h"
#include "vgpu.h"
#include "virgl_proto.h"

#include <string.h>

#define DEMO_W 128
#define DEMO_H 128
#define OWN 0

static uint32_t g_ctx;
static uint32_t g_tgt;
static uint32_t g_tex;
static uint32_t g_mesh_tri;
static uint32_t g_mesh_depth;
static uint32_t g_mesh_cube;
static uint32_t g_mesh_tcube;
static uint32_t g_mesh_vary;
static uint32_t g_mesh_light;
static uint32_t g_mesh_lit;
static uint32_t g_prog_tri;
static uint32_t g_prog_mvp;
static uint32_t g_prog_tex;
static uint32_t g_prog_vary;
static uint32_t g_prog_light;
static uint32_t g_prog_world;
static ShProgram *g_sh_tri;
static ShProgram *g_sh_mvp;
static ShProgram *g_sh_tex;
static ShProgram *g_sh_vary;
static ShProgram *g_sh_light;
static ShProgram *g_sh_world;
static ShShader *g_sh_keep[16];
static int g_nsh;
static uint32_t g_pix[DEMO_W * DEMO_H];

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

static int count_if(int (*pred)(uint32_t)) {
    int i;
    int n = 0;
    for (i = 0; i < DEMO_W * DEMO_H; ++i) {
        if (pred(g_pix[i])) {
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
    uint32_t cap = pick_capset();
    uint32_t ctx = 0;
    VgpuCreate3D info;
    if (cap == 0u || vgpu_ctx_create(VGPU_OWNER_KERNEL, cap, 1, &ctx) != 0) {
        serial_puts("FAIL: virgl resource cycle\n");
        return -1;
    }
    memset(&info, 0, sizeof info);
    info.target = VIRGL_TARGET_TEXTURE_2D;
    info.format = VIRGL_FORMAT_B8G8R8A8_UNORM;
    info.bind = VIRGL_BIND_RENDER_TARGET;
    info.width = 1;
    info.height = 1;
    info.depth = 1;
    info.array_size = 1;
    info.flags = 1u;
    for (i = 0; i < n; ++i) {
        uint32_t id = 0;
        if (vgpu_res_create_3d(VGPU_OWNER_KERNEL, &info, -1, 0, &id) != 0 ||
            vgpu_ctx_attach(ctx, id) != 0 || vgpu_ctx_detach(ctx, id) != 0 ||
            vgpu_res_unref(VGPU_OWNER_KERNEL, id) != 0) {
            serial_puts("FAIL: virgl resource cycle\n");
            (void)vgpu_ctx_destroy(VGPU_OWNER_KERNEL, ctx);
            return -1;
        }
    }
    if (vgpu_ctx_destroy(VGPU_OWNER_KERNEL, ctx) != 0 || vgpu_res_live_count() != before) {
        serial_puts("FAIL: virgl resource leak\n");
        return -1;
    }
    serial_puts("PASS: virgl resource stress ");
    serial_write_u64((uint64_t)n);
    serial_puts("\n");
    return 0;
}

static int keep_shader(ShShader *s) {
    if (!s || g_nsh >= 16) {
        return -1;
    }
    g_sh_keep[g_nsh++] = s;
    return 0;
}

static int build_prog(const char *vn, const char *vs, const char *fn, const char *fs,
                      ShProgram **out, uint32_t *inst) {
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
    *inst = gfx3d_prog_prepare(OWN, g_ctx, p);
    if (*inst == 0u) {
        serial_puts("FAIL: glsl shader submit\n");
        return -1;
    }
    *out = p;
    return 0;
}

static void lay2(Gfx3DLayout *L, int stride, int c0, int o0, int c1, int o1) {
    memset(L, 0, sizeof *L);
    L->stride = stride;
    L->nelem = 2;
    L->location[0] = 0;
    L->location[1] = 1;
    L->components[0] = c0;
    L->components[1] = c1;
    L->offset[0] = o0;
    L->offset[1] = o1;
}

static void lay3(Gfx3DLayout *L, int stride) {
    memset(L, 0, sizeof *L);
    L->stride = stride;
    L->nelem = 3;
    L->location[0] = 0;
    L->location[1] = 1;
    L->location[2] = 2;
    L->components[0] = 4;
    L->components[1] = 4;
    L->components[2] = 2;
    L->offset[0] = 0;
    L->offset[1] = 16;
    L->offset[2] = 32;
}

static uint32_t mesh_up(const Gfx3DLayout *lay, const void *verts, int nbytes, const uint16_t *idx,
                        int nidx) {
    uint32_t m = gfx3d_mesh_create(OWN, GFX3D_USAGE_STATIC);
    if (m == 0u) {
        return 0;
    }
    if (gfx3d_mesh_upload(OWN, m, lay, verts, nbytes, idx, nidx) != 0) {
        (void)gfx3d_mesh_destroy(OWN, m);
        return 0;
    }
    return m;
}

static int draw_mesh(uint32_t prog, uint32_t mesh, int depth, int cull, uint32_t tex, int clear,
                     float r, float g, float b, float a, int cdepth) {
    if (gfx3d_begin(OWN, g_ctx, g_tgt) != 0) {
        return -1;
    }
    if (clear && gfx3d_clear(OWN, g_ctx, r, g, b, a, cdepth) != 0) {
        return -1;
    }
    if (gfx3d_depth(OWN, g_ctx, depth) != 0 || gfx3d_cull(OWN, g_ctx, cull) != 0 ||
        gfx3d_use(OWN, g_ctx, prog) != 0 || gfx3d_bind_tex(OWN, g_ctx, 0, tex) != 0 ||
        gfx3d_draw(OWN, g_ctx, mesh) != 0 || gfx3d_end(OWN, g_ctx) != 0) {
        return -1;
    }
    return gfx3d_target_read(OWN, g_tgt, g_pix, DEMO_W * DEMO_H);
}

static int clear_only(float r, float g, float b, float a, int depth) {
    if (gfx3d_begin(OWN, g_ctx, g_tgt) != 0 || gfx3d_clear(OWN, g_ctx, r, g, b, a, depth) != 0 ||
        gfx3d_end(OWN, g_ctx) != 0) {
        return -1;
    }
    return gfx3d_target_read(OWN, g_tgt, g_pix, DEMO_W * DEMO_H);
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
    int i;
    for (i = 0; i < 16; ++i) {
        m[i] = 0.f;
    }
    m[0] = 1.1f * c;
    m[2] = -0.5f * s;
    m[3] = s;
    m[5] = 1.1f;
    m[8] = 1.1f * s;
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

static int test_clear(void) {
    int n;
    if (clear_only(0.f, 1.f, 0.f, 1.f, 0) != 0) {
        serial_puts("FAIL: virgl clear submit\n");
        return -1;
    }
    n = count_if(pix_green);
    if (n < (DEMO_W * DEMO_H * 8) / 10) {
        serial_puts("FAIL: virgl clear pixels ");
        serial_write_u64((uint64_t)n);
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: virgl clear\n");
    serial_puts("PASS: gfx3 virgl backend\n");
    return 0;
}

static int test_tri(void) {
    int red;
    int green;
    if (draw_mesh(g_prog_tri, g_mesh_tri, 0, 0, 0, 0, 0.f, 0.f, 0.f, 1.f, 0) != 0) {
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
    int red;
    int green;
    if (draw_mesh(g_prog_tri, g_mesh_depth, 1, 0, 0, 1, 0.f, 0.f, 1.f, 1.f, 1) != 0) {
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
    if (set_mvp(textured ? g_sh_tex : g_sh_mvp, 28.0f) != 0) {
        return -1;
    }
    if (textured) {
        int red;
        int blue;
        if (draw_mesh(g_prog_tex, g_mesh_tcube, 1, 1, g_tex, 1, 0.f, 0.f, 0.f, 1.f, 1) != 0) {
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
    if (draw_mesh(g_prog_mvp, g_mesh_cube, 1, 1, 0, 1, 0.f, 0.f, 0.f, 1.f, 1) != 0) {
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
    int red;
    int green;
    int blue;
    if (draw_mesh(g_prog_vary, g_mesh_vary, 0, 0, 0, 1, 0.f, 0.f, 0.f, 1.f, 0) != 0) {
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
    if (sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "model"), id, 16) != 0 ||
        sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "view"), id, 16) != 0 ||
        sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "projection"), id, 16) != 0 ||
        sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "lightPosition"), light, 3) != 0 ||
        sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "lightColor"), lcol, 3) != 0 ||
        sh_uniform_set(g_sh_light, sh_uniform_find(g_sh_light, "ambientColor"), amb, 3) != 0) {
        serial_puts("FAIL: virgl lighting uniforms\n");
        return -1;
    }
    if (draw_mesh(g_prog_light, g_mesh_light, 0, 0, 0, 1, 0.f, 0.f, 0.f, 1.f, 0) != 0) {
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

static int test_switch(void) {
    Gfx3DStats st;
    uint32_t before;
    int red;
    gfx3d_stats(&st);
    before = st.uploads;
    if (draw_mesh(g_prog_tri, g_mesh_tri, 0, 0, 0, 1, 0.f, 0.f, 1.f, 1.f, 0) != 0) {
        serial_puts("FAIL: virgl shader switch submit\n");
        return -1;
    }
    gfx3d_stats(&st);
    if (st.uploads != before) {
        serial_puts("FAIL: gfx3 persistent mesh\n");
        return -1;
    }
    red = count_if(pix_red);
    if (red < 40) {
        serial_puts("FAIL: virgl shader switch\n");
        return -1;
    }
    serial_puts("PASS: virgl shader switch\n");
    serial_puts("PASS: gfx3 persistent mesh\n");
    return 0;
}

static int test_lit_mesh(void) {
    float model[16];
    float view[16];
    float proj[16];
    float nm[9];
    float light[3];
    float lcol[3];
    float amb[3];
    Gfx3DStats st;
    uint32_t before;
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
    if (sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "model"), model, 16) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "view"), view, 16) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "projection"), proj, 16) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "normalMatrix"), nm, 9) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "lightPosition"), light, 3) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "lightColor"), lcol, 3) != 0 ||
        sh_uniform_set(g_sh_world, sh_uniform_find(g_sh_world, "ambientColor"), amb, 3) != 0) {
        serial_puts("FAIL: virgl lit mesh uniforms\n");
        return -1;
    }
    gfx3d_stats(&st);
    before = st.uploads;
    if (draw_mesh(g_prog_world, g_mesh_lit, 0, 0, g_tex, 1, 0.f, 0.f, 0.f, 1.f, 0) != 0) {
        serial_puts("FAIL: virgl lit mesh submit\n");
        return -1;
    }
    gfx3d_stats(&st);
    if (st.uploads != before) {
        serial_puts("FAIL: gfx3 texture\n");
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
    serial_puts("PASS: gfx3 texture\n");
    return 0;
}

static int present_scanout(void) {
    if (gfx3d_present_scanout(OWN, g_tgt) != 0) {
        serial_puts("FAIL: virgl present\n");
        return -1;
    }
    serial_puts("PASS: virgl present\n");
    (void)gfx3d_scanout_primary();
    return 0;
}

static void free_shaders(void) {
    int i;
    sh_program_free(g_sh_tri);
    sh_program_free(g_sh_mvp);
    sh_program_free(g_sh_tex);
    sh_program_free(g_sh_vary);
    sh_program_free(g_sh_light);
    sh_program_free(g_sh_world);
    g_sh_tri = 0;
    g_sh_mvp = 0;
    g_sh_tex = 0;
    g_sh_vary = 0;
    g_sh_light = 0;
    g_sh_world = 0;
    for (i = 0; i < g_nsh; ++i) {
        sh_shader_free(g_sh_keep[i]);
        g_sh_keep[i] = 0;
    }
    g_nsh = 0;
}

static void drop_u(uint32_t *h, int (*fn)(int, uint32_t)) {
    if (*h) {
        (void)fn(OWN, *h);
        *h = 0;
    }
}

static void cleanup(void) {
    drop_u(&g_mesh_tri, gfx3d_mesh_destroy);
    drop_u(&g_mesh_depth, gfx3d_mesh_destroy);
    drop_u(&g_mesh_cube, gfx3d_mesh_destroy);
    drop_u(&g_mesh_tcube, gfx3d_mesh_destroy);
    drop_u(&g_mesh_vary, gfx3d_mesh_destroy);
    drop_u(&g_mesh_light, gfx3d_mesh_destroy);
    drop_u(&g_mesh_lit, gfx3d_mesh_destroy);
    drop_u(&g_tex, gfx3d_tex_destroy);
    drop_u(&g_prog_tri, gfx3d_prog_destroy);
    drop_u(&g_prog_mvp, gfx3d_prog_destroy);
    drop_u(&g_prog_tex, gfx3d_prog_destroy);
    drop_u(&g_prog_vary, gfx3d_prog_destroy);
    drop_u(&g_prog_light, gfx3d_prog_destroy);
    drop_u(&g_prog_world, gfx3d_prog_destroy);
    drop_u(&g_tgt, gfx3d_target_destroy);
    drop_u(&g_ctx, gfx3d_context_destroy);
    free_shaders();
}

static int make_assets(void) {
    static const float tri[] = {
        -0.7f, -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.7f,  -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f,  0.7f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    static const float depthv[] = {
        -0.55f, -0.55f, -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.55f, -0.55f, -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f,   0.55f,  -0.4f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -0.55f, -0.55f, 0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.55f,  -0.55f, 0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,   0.55f,  0.6f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
    };
    static const float cube[] = {
        -0.5f, -0.5f, 0.5f,  1, 0, 0, 1, 1, 0.5f, -0.5f, 0.5f,  1, 0, 0, 1, 1, 0.5f,  0.5f,  0.5f,  1, 0, 0, 1, 1,
        -0.5f, 0.5f,  0.5f,  1, 0, 0, 1, 1, 0.5f, -0.5f, -0.5f, 1, 0, 1, 0, 1, 0.5f, 0.5f,  -0.5f, 1, 0, 1, 0, 1,
        -0.5f, 0.5f,  -0.5f, 1, 0, 1, 0, 1, -0.5f, -0.5f, -0.5f, 1, 0, 1, 0, 1,
    };
    static const float tcube[] = {
        -0.5f, -0.5f, 0.5f, 1, 0, 0, 0.5f, -0.5f, 0.5f, 1, 1, 0, 0.5f,  0.5f,  0.5f, 1, 1, 1,
        -0.5f, 0.5f,  0.5f, 1, 0, 1, 0.5f, -0.5f, -0.5f, 1, 0, 0, 0.5f, 0.5f, -0.5f, 1, 1, 0,
        -0.5f, 0.5f,  -0.5f, 1, 1, 1, -0.5f, -0.5f, -0.5f, 1, 0, 1,
    };
    static const uint16_t idx[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                                   1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 0, 1, 5, 0, 5, 4};
    static const float vary[] = {
        -0.8f, -0.7f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.8f, -0.7f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.0f,  0.7f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
    };
    static const float light[] = {
        -0.85f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  0.0f, -0.05f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        -0.45f, 0.70f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.05f,  -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f,
        0.85f,  -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.45f,  0.70f,  0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f,
    };
    static const float lit[] = {
        -0.85f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f, -0.05f, -0.65f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f,   0.0f,   1.0f, 0.0f, -0.45f, 0.70f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  0.0f,  0.0f, 1.0f,  0.05f, -0.65f,
        0.0f,   1.0f,   0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.85f, -0.65f, 0.0f, 1.0f,  0.0f, 0.0f,  -1.0f, 0.0f,
        1.0f,   0.0f,   0.45f, 0.70f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
    };
    uint32_t px[16];
    int i;
    Gfx3DLayout lay;
    lay2(&lay, 32, 4, 0, 4, 16);
    g_mesh_tri = mesh_up(&lay, tri, (int)sizeof tri, 0, 0);
    g_mesh_depth = mesh_up(&lay, depthv, (int)sizeof depthv, 0, 0);
    g_mesh_cube = mesh_up(&lay, cube, (int)sizeof cube, idx, 36);
    lay2(&lay, 24, 4, 0, 2, 16);
    g_mesh_tcube = mesh_up(&lay, tcube, (int)sizeof tcube, idx, 36);
    lay2(&lay, 32, 4, 0, 4, 16);
    g_mesh_vary = mesh_up(&lay, vary, (int)sizeof vary, 0, 0);
    g_mesh_light = mesh_up(&lay, light, (int)sizeof light, 0, 0);
    lay3(&lay, 40);
    g_mesh_lit = mesh_up(&lay, lit, (int)sizeof lit, 0, 0);
    if (!g_mesh_tri || !g_mesh_depth || !g_mesh_cube || !g_mesh_tcube || !g_mesh_vary || !g_mesh_light ||
        !g_mesh_lit) {
        return -1;
    }
    g_tex = gfx3d_tex_create(OWN, 4, 4);
    if (!g_tex) {
        return -1;
    }
    for (i = 0; i < 16; ++i) {
        px[i] = ((i / 2) & 1) ? 0xFF0000FFu : 0xFFFF0000u;
    }
    if (gfx3d_tex_upload(OWN, g_tex, px, 4, 4) != 0) {
        return -1;
    }
    return 0;
}

int virgl_demo_run(void) {
    uint32_t cap = pick_capset();
    int cycles = bootflag_gfx_stress() ? 64 : 4;
    int res_cycles = bootflag_gfx_stress() ? 128 : 4;
    if (cap == 0u) {
        serial_puts("reason no VIRGL capset\n");
        return -1;
    }
    if (gfx3d_backend() != GFX3D_VIRGL && gfx3d_boot(GFX3D_VIRGL) != 0) {
        return -1;
    }
    if (ctx_cycle(cap, cycles) != 0 || res3d_cycle(res_cycles) != 0) {
        return -1;
    }
    g_ctx = gfx3d_context_create(OWN);
    g_tgt = g_ctx ? gfx3d_target_create(OWN, g_ctx, DEMO_W, DEMO_H) : 0;
    if (!g_ctx || !g_tgt || make_assets() != 0) {
        cleanup();
        return -1;
    }
    if (build_prog("tri.vert", SH_SRC_TRI_VERT, "tri.frag", SH_SRC_TRI_FRAG, &g_sh_tri, &g_prog_tri) !=
            0 ||
        build_prog("mvp.vert", SH_SRC_MVP_VERT, "tri.frag", SH_SRC_TRI_FRAG, &g_sh_mvp, &g_prog_mvp) !=
            0 ||
        build_prog("tex.vert", SH_SRC_TEX_VERT, "tex.frag", SH_SRC_TEX_FRAG, &g_sh_tex, &g_prog_tex) !=
            0 ||
        build_prog("vary.vert", SH_SRC_VARY_VERT, "vary.frag", SH_SRC_VARY_FRAG, &g_sh_vary,
                   &g_prog_vary) != 0 ||
        build_prog("light.vert", SH_SRC_LIGHT_VERT, "light.frag", SH_SRC_LIGHT_FRAG, &g_sh_light,
                   &g_prog_light) != 0 ||
        build_prog("world.vert", SH_SRC_WORLD_VERT, "world.frag", SH_SRC_WORLD_FRAG, &g_sh_world,
                   &g_prog_world) != 0) {
        cleanup();
        return -1;
    }
    serial_puts("PASS: glsl compile\n");
    serial_puts("PASS: shader mine link\n");
    if (test_clear() != 0 || test_tri() != 0 || test_depth() != 0 || test_cube(0) != 0 ||
        test_cube(1) != 0 || test_varying() != 0 || test_lighting() != 0 || test_switch() != 0 ||
        test_lit_mesh() != 0 || present_scanout() != 0) {
        cleanup();
        return -1;
    }
    cleanup();
    return 0;
}
