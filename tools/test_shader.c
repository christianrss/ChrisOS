#include "shader/sh_pub.h"

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
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    n = ftell(f);
    if (n < 0 || n > SH_SRC_MAX) {
        fclose(f);
        return 0;
    }
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

static int contains(const char *s, const char *n) {
    return s && n && strstr(s, n) != 0;
}

static void ident4(float m[16]) {
    int i;
    for (i = 0; i < 16; ++i) {
        m[i] = 0.f;
    }
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static ShProgram *link2(const char *vn, const char *vs, const char *fn, const char *fs,
                        ShShader **ov, ShShader **of) {
    ShShader *a = sh_compile(SH_STAGE_VERTEX, vn, vs);
    ShShader *b = sh_compile(SH_STAGE_FRAGMENT, fn, fs);
    ShProgram *p = sh_program_create();
    *ov = a;
    *of = b;
    if (!a || !b || !p) {
        return p;
    }
    if (!sh_shader_ok(a)) {
        printf("VS %s\n%s\n", vn, sh_shader_log(a));
        return p;
    }
    if (!sh_shader_ok(b)) {
        printf("FS %s\n%s\n", fn, sh_shader_log(b));
        return p;
    }
    if (sh_program_attach(p, a) != 0 || sh_program_attach(p, b) != 0) {
        return p;
    }
    (void)sh_program_link(p);
    return p;
}

static void drop(ShProgram *p, ShShader *a, ShShader *b) {
    sh_program_free(p);
    sh_shader_free(a);
    sh_shader_free(b);
}

static void test_constant(void) {
    const char *vs = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const char *fs = "out vec4 c; void main() { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p = link2("const.vert", vs, "const.frag", fs, &a, &b);
    float attr[8] = {0, 0, 0, 1, 0, 0, 0, 0};
    float pos[4];
    float vary[8][4];
    expect(p && sh_program_ok(p), "constant link");
    expect(contains(sh_shader_ir(a), "store"), "ir dump");
    expect(contains(sh_program_tgsi(p, SH_STAGE_VERTEX), "VERT"), "tgsi vert");
    expect(contains(sh_program_tgsi(p, SH_STAGE_VERTEX), "POSITION"), "tgsi position");
    expect(contains(sh_program_tgsi(p, SH_STAGE_VERTEX), "END"), "tgsi end");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "soft vs");
    expect(pos[0] == 0.f && pos[1] == 0.f && pos[2] == 0.f && pos[3] == 1.f, "pos 0,0,0,1");
    drop(p, a, b);
}

static void test_errors(void) {
    const char *bad = "void main() { vec2 v = vec2(1.0, 2.0); gl_Position = vec4(v.z, 0.0, 0.0, 1.0); }\n";
    const char *pp = "#extension GL_ARB_gpu_shader5 : enable\nvoid main() {}\n";
    ShShader *s = sh_compile(SH_STAGE_VERTEX, "sw.vert", bad);
    expect(s && !sh_shader_ok(s), "swizzle rejected");
    expect(contains(sh_shader_log(s), "sw.vert:"), "diag name");
    expect(contains(sh_shader_log(s), "error:"), "diag error");
    expect(contains(sh_shader_log(s), "invalid swizzle"), "swizzle text");
    sh_shader_free(s);
    s = sh_compile(SH_STAGE_VERTEX, "pp.vert", pp);
    expect(s && !sh_shader_ok(s), "directive rejected");
    expect(contains(sh_shader_log(s), "unsupported preprocessor"), "directive text");
    sh_shader_free(s);
    s = sh_compile(SH_STAGE_GEOMETRY, "g.geom", "void main() {}\n");
    expect(s && !sh_shader_ok(s), "geometry rejected");
    sh_shader_free(s);
}

static void test_mvp(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/mvp.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/tri.frag");
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p;
    float model[16];
    float view[16];
    float proj[16];
    float attr[8] = {1, 1, 1, 1, 0, 0, 0, 1};
    float pos[4];
    float vary[8][4];
    int loc;
    expect(vs && fs, "mvp files");
    p = link2("mvp.vert", vs ? vs : "", "tri.frag", fs ? fs : "", &a, &b);
    expect(p && sh_program_ok(p), "mvp link");
    expect(contains(sh_program_tgsi(p, SH_STAGE_VERTEX), "CONST[0]"), "mvp const");
    expect(contains(sh_program_tgsi(p, SH_STAGE_VERTEX), "MUL"), "mvp mul");
    ident4(view);
    ident4(proj);
    memset(model, 0, sizeof model);
    model[0] = 2.f;
    model[5] = 3.f;
    model[10] = 4.f;
    model[12] = 5.f;
    model[13] = 6.f;
    model[14] = 7.f;
    model[15] = 1.f;
    loc = sh_uniform_find(p, "model");
    expect(loc == 0, "model location");
    expect(sh_uniform_set(p, loc, model, 16) == 0, "set model");
    expect(sh_uniform_set(p, sh_uniform_find(p, "view"), view, 16) == 0, "set view");
    expect(sh_uniform_set(p, sh_uniform_find(p, "projection"), proj, 16) == 0, "set proj");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "mvp exec");
    expect(pos[0] > 6.9f && pos[0] < 7.1f && pos[1] > 8.9f && pos[1] < 9.1f && pos[2] > 10.9f &&
               pos[2] < 11.1f && pos[3] > 0.9f && pos[3] < 1.1f,
           "column major 7,9,11,1");
    expect(strlen(sh_program_tgsi(p, SH_STAGE_VERTEX)) < 3500u, "mvp tgsi budget");
    drop(p, a, b);
    free(vs);
    free(fs);
}

static void test_texture(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/tex.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/tex.frag");
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p;
    float model[16];
    float attr[8] = {0, 0, 0, 1, 0, 0, 0, 0};
    float pos[4];
    float vary[8][4];
    float color[4];
    uint8_t tex[16];
    int discarded = 0;
    expect(vs && fs, "tex files");
    p = link2("tex.vert", vs ? vs : "", "tex.frag", fs ? fs : "", &a, &b);
    expect(p && sh_program_ok(p), "tex link");
    expect(sh_sampler_find(p, "diffuseTexture") == 0, "sampler slot");
    expect(contains(sh_program_tgsi(p, SH_STAGE_FRAGMENT), "TEX"), "tgsi tex");
    expect(contains(sh_program_tgsi(p, SH_STAGE_FRAGMENT), "SAMP[0]"), "tgsi samp");
    ident4(model);
    sh_uniform_set(p, sh_uniform_find(p, "model"), model, 16);
    sh_uniform_set(p, sh_uniform_find(p, "view"), model, 16);
    sh_uniform_set(p, sh_uniform_find(p, "projection"), model, 16);
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "tex vs");
    memset(tex, 0, sizeof tex);
    tex[2] = 255;
    tex[3] = 255;
    expect(sh_soft_fs(p, vary, 0, tex, 2, 2, color, &discarded) == 0, "tex fs");
    expect(color[0] > 0.9f && color[3] > 0.9f, "sampled red");
    drop(p, a, b);
    free(vs);
    free(fs);
}

static void test_light(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/light.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/light.frag");
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p;
    float id[16];
    float attr[8] = {0, 0, 0, 0, 0, 0, 1, 0};
    float pos[4];
    float vary[8][4];
    float color[4];
    float light[3] = {0, 0, 2};
    float lcol[3] = {1, 0, 0};
    float amb[3] = {0.1f, 0.1f, 0.1f};
    int discarded = 0;
    expect(vs && fs, "light files");
    p = link2("light.vert", vs ? vs : "", "light.frag", fs ? fs : "", &a, &b);
    expect(p && sh_program_ok(p), "light link");
    expect(strlen(sh_program_tgsi(p, SH_STAGE_VERTEX)) < 3500u, "light vs budget");
    expect(strlen(sh_program_tgsi(p, SH_STAGE_FRAGMENT)) < 3500u, "light fs budget");
    ident4(id);
    sh_uniform_set(p, sh_uniform_find(p, "model"), id, 16);
    sh_uniform_set(p, sh_uniform_find(p, "view"), id, 16);
    sh_uniform_set(p, sh_uniform_find(p, "projection"), id, 16);
    sh_uniform_set(p, sh_uniform_find(p, "lightPosition"), light, 3);
    sh_uniform_set(p, sh_uniform_find(p, "lightColor"), lcol, 3);
    sh_uniform_set(p, sh_uniform_find(p, "ambientColor"), amb, 3);
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "light vs");
    expect(sh_soft_fs(p, vary, 0, 0, 0, 0, color, &discarded) == 0, "light fs");
    expect(color[0] > 0.8f && color[1] < 0.3f, "lambert front");
    attr[6] = -1.f;
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "light vs back");
    expect(sh_soft_fs(p, vary, 0, 0, 0, 0, color, &discarded) == 0, "light fs back");
    expect(color[0] < 0.4f && color[0] > 0.05f, "lambert back ambient");
    drop(p, a, b);
    free(vs);
    free(fs);
}

static void test_world(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/world.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/world.frag");
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p;
    expect(vs && fs, "world files");
    p = link2("world.vert", vs ? vs : "", "world.frag", fs ? fs : "", &a, &b);
    expect(p && sh_program_ok(p), "world link");
    expect(sh_attrib_count(p) == 3, "world attribs");
    expect(strlen(sh_program_tgsi(p, SH_STAGE_VERTEX)) < 3500u, "world vs budget");
    expect(strlen(sh_program_tgsi(p, SH_STAGE_FRAGMENT)) < 3500u, "world fs budget");
    drop(p, a, b);
    free(vs);
    free(fs);
}

static void test_link_mismatch(void) {
    const char *vs = "out vec3 normal; void main() { normal = vec3(0.0); gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const char *fs = "in vec2 normal; out vec4 c; void main() { c = vec4(normal, 0.0, 1.0); }\n";
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p = link2("mm.vert", vs, "mm.frag", fs, &a, &b);
    expect(p && !sh_program_ok(p), "mismatch fails link");
    expect(contains(sh_program_log(p), "type mismatch"), "mismatch text");
    drop(p, a, b);
}

static void test_flow(void) {
    const char *vs =
        "void main() {\n"
        "  float x = 0.2;\n"
        "  if (x > 0.5) {\n"
        "    gl_Position = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "  } else {\n"
        "    gl_Position = vec4(0.0, 1.0, 0.0, 1.0);\n"
        "  }\n"
        "}\n";
    const char *fs = "out vec4 c; void main() { c = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const char *loop =
        "void main() {\n"
        "  float s = 0.0;\n"
        "  for (int i = 0; i < 4; i++) {\n"
        "    s = s + 1.0;\n"
        "  }\n"
        "  gl_Position = vec4(s, 0.0, 0.0, 1.0);\n"
        "}\n";
    const char *fn =
        "float saturate(float x) {\n"
        "  return clamp(x, 0.0, 1.0);\n"
        "}\n"
        "void main() {\n"
        "  float y = saturate(-2.0);\n"
        "  gl_Position = vec4(y, 0.0, 0.0, 1.0);\n"
        "}\n";
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p = link2("if.vert", vs, "if.frag", fs, &a, &b);
    float attr[4] = {0, 0, 0, 1};
    float pos[4];
    float vary[8][4];
    expect(p && sh_program_ok(p), "if link");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "if exec");
    expect(pos[1] > 0.9f && pos[0] < 0.1f, "else branch");
    drop(p, a, b);
    p = link2("for.vert", loop, "if.frag", fs, &a, &b);
    expect(p && sh_program_ok(p), "for link");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "for exec");
    expect(pos[0] > 3.9f && pos[0] < 4.1f, "unroll 4");
    drop(p, a, b);
    p = link2("fn.vert", fn, "if.frag", fs, &a, &b);
    expect(p && sh_program_ok(p), "fn link");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "fn exec");
    expect(pos[0] > -0.1f && pos[0] < 0.1f, "saturate");
    drop(p, a, b);
}

static void test_discard_sin(void) {
    const char *vs = "out float a; void main() { a = 0.2; gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const char *fs =
        "in float a;\n"
        "out vec4 c;\n"
        "void main() {\n"
        "  if (a < 0.5) { discard; }\n"
        "  c = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";
    const char *proc =
        "uniform float time;\n"
        "out vec4 c;\n"
        "void main() {\n"
        "  c = vec4(0.5 + 0.5 * sin(time), 0.0, 0.0, 1.0);\n"
        "}\n";
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p = link2("d.vert", vs, "d.frag", fs, &a, &b);
    float attr[4] = {0, 0, 0, 1};
    float pos[4];
    float vary[8][4];
    float color[4];
    float t = 0.f;
    int discarded = 0;
    expect(p && sh_program_ok(p), "discard link");
    expect(contains(sh_program_tgsi(p, SH_STAGE_FRAGMENT), "KILL"), "tgsi kill");
    expect(sh_soft_vs(p, attr, pos, vary) == 0, "discard vs");
    expect(sh_soft_fs(p, vary, 0, 0, 0, 0, color, &discarded) == 0, "discard fs");
    expect(discarded == 1, "discarded");
    drop(p, a, b);
    a = 0;
    b = sh_compile(SH_STAGE_FRAGMENT, "proc.frag", proc);
    expect(b && sh_shader_ok(b), "sin compile");
    p = sh_program_create();
    a = sh_compile(SH_STAGE_VERTEX, "p.vert",
                   "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n");
    expect(sh_program_attach(p, a) == 0 && sh_program_attach(p, b) == 0 && sh_program_link(p) == 0,
           "sin link");
    sh_uniform_set(p, sh_uniform_find(p, "time"), &t, 1);
    discarded = 0;
    expect(sh_soft_fs(p, 0, 0, 0, 0, 0, color, &discarded) == 0, "sin exec");
    expect(color[0] > 0.4f && color[0] < 0.6f, "sin 0");
    drop(p, a, b);
}

static void test_raster(void) {
    char *vs = read_file("kernel/gfx/shader/glsl/tri.vert");
    char *fs = read_file("kernel/gfx/shader/glsl/tri.frag");
    ShShader *a = 0;
    ShShader *b = 0;
    ShProgram *p;
    float v0[8] = {-0.8f, -0.8f, 0, 1, 1, 0, 0, 1};
    float v1[8] = {0.8f, -0.8f, 0, 1, 1, 0, 0, 1};
    float v2[8] = {0, 0.8f, 0, 1, 1, 0, 0, 1};
    uint32_t pix[32 * 32];
    int i;
    int red = 0;
    p = link2("tri.vert", vs ? vs : "", "tri.frag", fs ? fs : "", &a, &b);
    expect(p && sh_program_ok(p), "raster link");
    memset(pix, 0, sizeof pix);
    expect(sh_soft_triangle(p, v0, v1, v2, 8, 0, 0, 0, pix, 32, 32) == 0, "raster");
    for (i = 0; i < 32 * 32; ++i) {
        if (((pix[i] >> 16) & 255u) > 200u) {
            red++;
        }
    }
    expect(red > 20, "raster red");
    drop(p, a, b);
    free(vs);
    free(fs);
}

static void test_csi_guest_fuzz(void) {
    const char *vs = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    ShShader *s = sh_compile(SH_STAGE_VERTEX, "csi.vert", vs);
    uint8_t blob[8192];
    int n;
    int i;
    int live;
    ShShader *loaded;
    expect(s && sh_shader_ok(s), "csi compile");
    n = sh_shader_save_csi(s, blob, (int)sizeof blob);
    expect(n > 32, "csi save");
    loaded = sh_shader_load_csi(blob, n);
    expect(loaded && sh_shader_ok(loaded), "csi load");
    if (n > 40) {
        blob[40] ^= 1u;
    }
    expect(sh_shader_load_csi(blob, n) == 0, "csi corrupt");
    sh_shader_free(loaded);
    sh_shader_free(s);
    live = sh_live_count();
    for (i = 0; i < 1000; ++i) {
        ShShader *t = sh_compile(SH_STAGE_VERTEX, "leak.vert", vs);
        sh_shader_free(t);
    }
    expect(sh_live_count() == live, "no live leak");
    {
        const char *bad[] = {"", "@@@", "/*", "((((", "void main( {", "1e", "float x = ;"};
        char huge[80];
        memset(huge, 'A', sizeof huge);
        huge[79] = 0;
        for (i = 0; i < 7; ++i) {
            ShShader *t = sh_compile(SH_STAGE_VERTEX, "fuzz.vert", bad[i]);
            expect(t != 0, "fuzz returns");
            sh_shader_free(t);
        }
        {
            ShShader *t = sh_compile(SH_STAGE_VERTEX, "huge.vert", huge);
            expect(t != 0, "huge ident");
            sh_shader_free(t);
        }
    }
    {
        int id = sh_guest_compile(7, SH_STAGE_VERTEX, "g.vert", vs);
        int other;
        expect(id > 0, "guest compile");
        expect(sh_guest_shader_ok(7, id) == 1, "guest ok");
        expect(sh_guest_shader_drop(9, id) == -2, "foreign drop");
        other = sh_guest_prog_make(7);
        expect(other > 0, "guest prog");
        sh_guest_drop_owner(7);
        expect(sh_guest_shader_ok(7, id) == 0, "owner cleanup");
    }
}

int main(void) {
    int before = sh_live_count();
    test_constant();
    test_errors();
    test_mvp();
    test_texture();
    test_light();
    test_world();
    test_link_mismatch();
    test_flow();
    test_discard_sin();
    test_raster();
    test_csi_guest_fuzz();
    expect(sh_live_count() == before, "live restored");
    if (g_fail) {
        printf("shader tests failed %d\n", g_fail);
        return 1;
    }
    printf("shader tests ok\n");
    return 0;
}
