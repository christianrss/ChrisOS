#include "gfx3d.h"
#include "gfx3d_batch.h"
#include "math3d.h"
#include "shader/sh_pub.h"
#include "virgl_obj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL %s\n", msg);
        g_fail++;
    }
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    b = (char *)malloc((size_t)n + 1u);
    if (!b) {
        fclose(f);
        return 0;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return 0;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

static int close4(const float *a, const float *b) {
    int i;
    for (i = 0; i < 4; ++i) {
        float d = a[i] - b[i];
        if (d < 0.0f) {
            d = -d;
        }
        if (d > 0.002f) {
            return 0;
        }
    }
    return 1;
}

static void set_glsl(ShProgram *p, const char *name, const Mat4f *m) {
    float g[16];
    mat4f_to_glsl(m, g);
    expect(sh_uniform_set(p, sh_uniform_find(p, name), g, 16) == 0, name);
}

static void check_mvp(ShProgram *p, const char *tag, const Mat4f *model, const Mat4f *view,
                      const Mat4f *proj, float x, float y, float z) {
    Mat4f vm;
    Mat4f mvp;
    float cpu[4];
    float attr[8];
    float pos[4];
    float vary[8][4];
    int i;
    mat4f_mul(&vm, view, model);
    mat4f_mul(&mvp, proj, &vm);
    mat4f_transform4(&mvp, x, y, z, 1.0f, cpu);
    set_glsl(p, "model", model);
    set_glsl(p, "view", view);
    set_glsl(p, "projection", proj);
    for (i = 0; i < 8; ++i) {
        attr[i] = 0.0f;
    }
    attr[0] = x;
    attr[1] = y;
    attr[2] = z;
    attr[3] = 1.0f;
    expect(sh_soft_vs(p, attr, pos, vary) == 0, tag);
    if (!close4(cpu, pos)) {
        printf("FAIL %s cpu %.4f %.4f %.4f %.4f shader %.4f %.4f %.4f %.4f\n", tag, cpu[0], cpu[1],
               cpu[2], cpu[3], pos[0], pos[1], pos[2], pos[3]);
        g_fail++;
    }
}

static void test_matrix(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/mvp.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/tri.frag");
    ShShader *a;
    ShShader *b;
    ShProgram *p;
    Mat4f id;
    Mat4f model;
    Mat4f view;
    Mat4f proj;
    Mat4f t;
    expect(vs && fs, "shader files");
    a = sh_compile(SH_STAGE_VERTEX, "mvp.vert", vs ? vs : "");
    b = sh_compile(SH_STAGE_FRAGMENT, "tri.frag", fs ? fs : "");
    p = sh_program_create();
    expect(a && b && p && sh_shader_ok(a) && sh_shader_ok(b), "compile");
    expect(sh_program_attach(p, a) == 0 && sh_program_attach(p, b) == 0 && sh_program_link(p) == 0,
           "link");
    mat4f_identity(&id);
    check_mvp(p, "identity", &id, &id, &id, 0.25f, -0.5f, 0.75f);
    mat4f_translate(&model, 4.0f, -2.0f, 1.5f);
    check_mvp(p, "translation", &model, &id, &id, 1.0f, 2.0f, 3.0f);
    mat4f_rotate_x(&model, 90.0f);
    check_mvp(p, "rot x", &model, &id, &id, 0.2f, 1.0f, 0.0f);
    mat4f_rotate_y(&model, 90.0f);
    check_mvp(p, "rot y", &model, &id, &id, 1.0f, 0.3f, 0.0f);
    mat4f_rotate_z(&model, 90.0f);
    check_mvp(p, "rot z", &model, &id, &id, 1.0f, 0.0f, 0.4f);
    mat4f_scale(&model, 2.0f, 3.0f, 4.0f);
    check_mvp(p, "scale", &model, &id, &id, 1.0f, 1.0f, 1.0f);
    math3d_cam_set(0.0f, 1.5f, 5.0f, 0.0f, 0.0f);
    math3d_view(&view);
    check_mvp(p, "camera", &id, &view, &id, 0.0f, 1.5f, 0.0f);
    math3d_cam_set(0.0f, 0.0f, 0.0f, 90.0f, 0.0f);
    math3d_view(&view);
    check_mvp(p, "yaw", &id, &view, &id, 1.0f, 0.0f, -2.0f);
    math3d_cam_set(0.0f, 0.0f, 0.0f, 0.0f, 30.0f);
    math3d_view(&view);
    check_mvp(p, "pitch", &id, &view, &id, 0.0f, 0.0f, -3.0f);
    mat4f_perspective(&proj, 60.0f, 1.0f, 0.1f, 100.0f);
    mat4f_translate(&t, 0.0f, 0.0f, -5.0f);
    check_mvp(p, "perspective", &t, &id, &proj, 0.0f, 0.0f, 0.0f);
    mat4f_ortho(&proj, -2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 50.0f);
    check_mvp(p, "ortho", &id, &id, &proj, 1.0f, -1.0f, -2.0f);
    {
        uint32_t gen = sh_program_gen(p);
        ShShader *bad = sh_compile(SH_STAGE_VERTEX, "bad.vert", "void main(){ no_such(); }");
        expect(bad && !sh_shader_ok(bad), "bad shader");
        expect(sh_program_attach(p, bad) != 0 || sh_program_link(p) != 0, "invalid relink");
        expect(sh_program_ok(p) && sh_program_gen(p) == gen, "old program kept");
        sh_shader_free(bad);
    }
    sh_program_free(p);
    sh_shader_free(a);
    sh_shader_free(b);
    free(vs);
    free(fs);
}

static void test_objects(void) {
    VirglObjPool pool;
    uint32_t ids[VIRGL_OBJ_POOL_MAX];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    int i;
    virgl_obj_init(&pool);
    a = virgl_obj_alloc(&pool, 4u);
    b = virgl_obj_alloc(&pool, 4u);
    expect(a != 0u && b != 0u && a != b, "distinct handles");
    expect(virgl_obj_free(&pool, a) == 0, "free a");
    c = virgl_obj_alloc(&pool, 4u);
    expect(c != 0u && c != a && c != b && virgl_obj_live(&pool, a) == 0, "no live reuse");
    expect(virgl_obj_live_count(&pool) == 2, "live 2");
    expect(virgl_obj_peak(&pool) >= 2, "peak");
    virgl_obj_init(&pool);
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        ids[i] = virgl_obj_alloc(&pool, 1u);
        expect(ids[i] != 0u, "fill");
    }
    expect(virgl_obj_alloc(&pool, 1u) == 0u, "pool full");
    expect(virgl_obj_live_count(&pool) == VIRGL_OBJ_POOL_MAX, "live full");
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        expect(virgl_obj_free(&pool, ids[i]) == 0, "drain");
    }
    expect(virgl_obj_live_count(&pool) == 0, "live zero");
    expect(virgl_obj_peak(&pool) == VIRGL_OBJ_POOL_MAX, "peak full");
}

static int g_submits;

static int fake_submit(void *user, uint32_t ctx, const uint32_t *d, uint32_t n) {
    (void)user;
    (void)d;
    expect(ctx == 7u && n > 0u && n <= VIRGL_CMD_MAX, "submit bounds");
    g_submits++;
    return 0;
}

static void test_batch(void) {
    Gfx3DBatch b;
    VirglCmd *c;
    uint32_t i;
    g_submits = 0;
    expect(gfx3d_batch_open(&b, 7u, fake_submit, 0) == 0, "open");
    c = gfx3d_batch_cmd(&b);
    for (i = 0; i < VIRGL_CMD_MAX - 8u; ++i) {
        expect(virgl_cmd_u32(c, i) == 0, "fill");
    }
    expect(gfx3d_batch_reserve(&b, 32u) == 0, "rollover");
    expect(g_submits == 1, "one submit");
    expect(virgl_cmd_len(gfx3d_batch_cmd(&b)) == 0u, "reset");
    expect(gfx3d_batch_reserve(&b, VIRGL_CMD_MAX + 1u) != 0, "too big");
}

static void test_api(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/tri.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/tri.frag");
    ShShader *a;
    ShShader *b;
    ShProgram *p;
    Gfx3DLayout lay;
    uint32_t ctx;
    uint32_t tgt;
    uint32_t mesh;
    uint32_t inst;
    uint32_t pix[32 * 32];
    float verts[24];
    Gfx3DStats st;
    char log[256];
    int i;
    int red = 0;
    uint32_t first;
    uint32_t gen;
    expect(gfx3d_boot(GFX3D_MOCK) == 0, "mock boot");
    ctx = gfx3d_context_create(1);
    tgt = gfx3d_target_create(1, ctx, 16, 16);
    mesh = gfx3d_mesh_create(1, GFX3D_USAGE_STATIC);
    expect(ctx && tgt && mesh, "mock objects");
    expect(gfx3d_context_create(2) != 0u, "second owner");
    expect(gfx3d_mesh_destroy(2, mesh) != 0, "foreign destroy");
    memset(&lay, 0, sizeof lay);
    lay.stride = 32;
    lay.nelem = 2;
    lay.location[0] = 0;
    lay.location[1] = 1;
    lay.components[0] = 4;
    lay.components[1] = 4;
    lay.offset[1] = 16;
    a = sh_compile(SH_STAGE_VERTEX, "tri.vert", vs ? vs : "");
    b = sh_compile(SH_STAGE_FRAGMENT, "tri.frag", fs ? fs : "");
    p = sh_program_create();
    expect(sh_program_attach(p, a) == 0 && sh_program_attach(p, b) == 0 && sh_program_link(p) == 0,
           "tri link");
    expect(gfx3d_layout_ok(p, &lay), "layout");
    lay.components[0] = 1;
    expect(!gfx3d_layout_ok(p, &lay), "layout reject");
    lay.components[0] = 4;
    for (i = 0; i < 24; ++i) {
        verts[i] = 0.0f;
    }
    verts[0] = -0.8f;
    verts[1] = -0.8f;
    verts[3] = 1.0f;
    verts[4] = 1.0f;
    verts[7] = 1.0f;
    verts[8] = 0.8f;
    verts[9] = -0.8f;
    verts[11] = 1.0f;
    verts[12] = 1.0f;
    verts[15] = 1.0f;
    verts[17] = 0.8f;
    verts[19] = 1.0f;
    verts[20] = 1.0f;
    verts[23] = 1.0f;
    expect(gfx3d_mesh_upload(1, mesh, &lay, verts, 96, 0, 0) == 0, "upload");
    inst = gfx3d_prog_prepare(1, ctx, p);
    expect(inst != 0u, "prog");
    expect(gfx3d_begin(1, ctx, tgt) == 0 && gfx3d_use(1, ctx, inst) == 0 &&
               gfx3d_draw(1, ctx, mesh) == 0 && gfx3d_end(1, ctx) == 0,
           "mock frame");
    expect(gfx3d_mock_log(log, (int)sizeof log) > 0 && strstr(log, "begin") && strstr(log, "draw") &&
               strstr(log, "end"),
           "mock log");
    gfx3d_drop_owner(1);
    gfx3d_stats(&st);
    expect(st.live_mesh == 0 && st.live_ctx == 1 && st.live_prog == 0, "drop owner");
    gfx3d_drop_owner(2);
    expect(gfx3d_boot(GFX3D_SOFTWARE) == 0, "software");
    ctx = gfx3d_context_create(3);
    tgt = gfx3d_target_create(3, ctx, 32, 32);
    mesh = gfx3d_mesh_create(3, GFX3D_USAGE_STATIC);
    inst = gfx3d_prog_prepare(3, ctx, p);
    expect(gfx3d_mesh_upload(3, mesh, &lay, verts, 96, 0, 0) == 0, "soft upload");
    expect(gfx3d_begin(3, ctx, tgt) == 0 && gfx3d_clear(3, ctx, 0.0f, 1.0f, 0.0f, 1.0f, 1) == 0 &&
               gfx3d_use(3, ctx, inst) == 0 && gfx3d_draw(3, ctx, mesh) == 0 &&
               gfx3d_end(3, ctx) == 0,
           "soft frame");
    expect(gfx3d_target_read(3, tgt, pix, 32 * 32) == 0, "read");
    for (i = 0; i < 32 * 32; ++i) {
        if (((pix[i] >> 16) & 255u) > 200u) {
            red++;
        }
    }
    expect(red > 10, "software triangle");
    expect(gfx3d_target_resize(3, tgt, 320, 200) == 0, "320");
    expect(gfx3d_target_resize(3, tgt, 640, 480) == 0, "640");
    expect(gfx3d_target_resize(3, tgt, 800, 600) == 0, "800");
    expect(gfx3d_target_resize(3, tgt, 640, 480) == 0, "back 640");
    first = mesh;
    expect(gfx3d_mesh_destroy(3, mesh) == 0, "destroy mesh");
    mesh = gfx3d_mesh_create(3, GFX3D_USAGE_DYNAMIC);
    expect(mesh != 0u && mesh != first, "generation");
    gfx3d_stats(&st);
    gen = st.live_mesh;
    for (i = 0; i < 1000; ++i) {
        uint32_t m = gfx3d_mesh_create(3, GFX3D_USAGE_STATIC);
        uint32_t t = gfx3d_tex_create(3, 2, 2);
        uint32_t px = 0xFF0000FFu;
        expect(m && t && gfx3d_tex_upload(3, t, &px, 2, 2) == 0, "stress create");
        expect(gfx3d_mesh_destroy(3, m) == 0 && gfx3d_tex_destroy(3, t) == 0, "stress destroy");
    }
    gfx3d_stats(&st);
    expect(st.live_mesh == (int)gen && st.live_tex == 0, "stress baseline");
    gfx3d_drop_owner(3);
    gfx3d_stats(&st);
    expect(st.live_mesh == 0 && st.live_tex == 0 && st.live_target == 0 && st.live_ctx == 0 &&
               st.live_prog == 0,
           "clean");
    sh_program_free(p);
    sh_shader_free(a);
    sh_shader_free(b);
    free(vs);
    free(fs);
}

int main(void) {
    test_matrix();
    test_objects();
    test_batch();
    test_api();
    if (g_fail) {
        printf("test_gfx3d_abi: %d failures\n", g_fail);
        return 1;
    }
    printf("test_gfx3d_abi: ok\n");
    return 0;
}
