#include "sh_int.h"

#ifdef SH_HOST
#include <stdlib.h>
static void *sh_alloc(unsigned long n) { return malloc(n); }
static void sh_free(void *p) { free(p); }
#else
#include "heap.h"
static void *sh_alloc(unsigned long n) { return kmalloc((uint64_t)n); }
static void sh_free(void *p) {
    if (p) {
        kfree(p);
    }
}
#endif

static int g_live;
static ShComp *g_cache[8];
static uint32_t g_cache_hash[8];
static int g_cache_stage[8];
static int g_cache_n;

static uint64_t sh_now(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static int slen(const char *s) {
    int n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        ++n;
    }
    return n;
}

static uint32_t sh_hash(int stage, const char *src, int n) {
    uint32_t h = 2166136261u ^ (uint32_t)(stage + SH_COMPILER_VERSION * 17);
    int i;
    for (i = 0; i < n; ++i) {
        h ^= (uint8_t)src[i];
        h *= 16777619u;
    }
    return h;
}

struct ShShader {
    ShComp *c;
    int ok;
    char tgsi[SH_TGSI_MAX];
    char ird[4096];
    char astd[2048];
};

struct ShProgram {
    ShShader *vs;
    ShShader *fs;
    int linked;
    char log[SH_LOG_MAX];
    int logn;
    char vs_tgsi[SH_TGSI_MAX];
    char fs_tgsi[SH_TGSI_MAX];
    int var_remap[8];
    int nattr;
    int nvary;
    int nuni;
    int nsamp;
    ShAttribInfo attr[8];
    ShVaryInfo vary[8];
    ShUniformInfo uni[32];
    float vs_words[128];
    float fs_words[128];
    int vs_nvec;
    int fs_nvec;
    uint32_t gen;
};

static void prog_log(ShProgram *p, const char *msg) {
    sh_app(p->log, SH_LOG_MAX, &p->logn, msg);
    sh_app(p->log, SH_LOG_MAX, &p->logn, "\n");
}

static int memcmp_src(const char *a, const char *b, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int cache_find(uint32_t h, int stage, const char *src, int n) {
    int i;
    for (i = 0; i < g_cache_n; ++i) {
        if (g_cache_hash[i] == h && g_cache_stage[i] == stage && g_cache[i] &&
            g_cache[i]->src_len == n && memcmp_src(g_cache[i]->src, src, n)) {
            return i;
        }
    }
    return -1;
}

ShShader *sh_compile(int stage, const char *name, const char *src) {
    ShShader *s;
    ShComp *c;
    int n;
    uint32_t h;
    int hit;
    uint64_t t0;
    if (!src) {
        return 0;
    }
    n = slen(src);
    s = (ShShader *)sh_alloc(sizeof *s);
    if (!s) {
        return 0;
    }
    memset(s, 0, sizeof *s);
    c = (ShComp *)sh_alloc(sizeof *c);
    if (!c) {
        sh_free(s);
        return 0;
    }
    memset(c, 0, sizeof *c);
    s->c = c;
    g_live++;
    if (name) {
        int i = 0;
        while (name[i] && i + 1 < (int)sizeof c->name) {
            c->name[i] = name[i];
            ++i;
        }
        c->name[i] = 0;
    } else {
        c->name[0] = 's';
        c->name[1] = 0;
    }
    c->stage = stage;
    if (stage != SH_STAGE_VERTEX && stage != SH_STAGE_FRAGMENT) {
        sh_err(c, -1, "geometry, tessellation, and compute shaders are not supported");
        return s;
    }
    if (n <= 0 || n >= SH_SRC_MAX) {
        sh_err(c, -1, n <= 0 ? "empty shader" : "shader source exceeds the size limit");
        return s;
    }
    memcpy(c->src, src, (unsigned long)n);
    c->src[n] = 0;
    c->src_len = n;
    h = sh_hash(stage, src, n);
    hit = cache_find(h, stage, src, n);
    if (hit >= 0) {
        memcpy(c, g_cache[hit], sizeof *c);
        s->ok = c->nerr == 0;
    } else {
        t0 = sh_now();
        sh_lex(c);
        c->cyc_lex = sh_now() - t0;
        if (c->nerr < SH_ERR_MAX) {
            t0 = sh_now();
            sh_parse(c);
            c->cyc_parse = sh_now() - t0;
        }
        if (c->nerr < SH_ERR_MAX) {
            t0 = sh_now();
            sh_sem(c);
            c->cyc_sem = sh_now() - t0;
        }
        if (c->nerr == 0) {
            t0 = sh_now();
            sh_opt(c);
            sh_verify(c);
            c->cyc_ir = sh_now() - t0;
        }
        s->ok = c->nerr == 0;
        if (s->ok && g_cache_n < 8) {
            g_cache[g_cache_n] = (ShComp *)sh_alloc(sizeof(ShComp));
            if (g_cache[g_cache_n]) {
                memcpy(g_cache[g_cache_n], c, sizeof *c);
                g_cache_hash[g_cache_n] = h;
                g_cache_stage[g_cache_n] = stage;
                g_cache_n++;
            }
        } else if (s->ok) {
            int slot = (int)(h & 7u);
            if (!g_cache[slot]) {
                g_cache[slot] = (ShComp *)sh_alloc(sizeof(ShComp));
            }
            if (g_cache[slot]) {
                memcpy(g_cache[slot], c, sizeof *c);
                g_cache_hash[slot] = h;
                g_cache_stage[slot] = stage;
            }
        }
    }
    if (s->ok) {
        t0 = sh_now();
        if (sh_emit_tgsi(c, s->tgsi, SH_TGSI_MAX, 0) != 0) {
            s->ok = 0;
        }
        c->cyc_tgsi = sh_now() - t0;
    }
    sh_dump_ir_buf(c, s->ird, (int)sizeof s->ird);
    sh_dump_ast_buf(c, s->astd, (int)sizeof s->astd);
    return s;
}

void sh_shader_free(ShShader *s) {
    if (!s) {
        return;
    }
    sh_free(s->c);
    sh_free(s);
    if (g_live > 0) {
        g_live--;
    }
}

int sh_shader_ok(const ShShader *s) { return s && s->ok; }
int sh_shader_stage(const ShShader *s) { return s && s->c ? s->c->stage : -1; }
const char *sh_shader_log(const ShShader *s) {
    if (!s || !s->c) {
        return "";
    }
    return s->c->log;
}
const char *sh_shader_tgsi(const ShShader *s) { return s ? s->tgsi : ""; }
const char *sh_shader_ir(const ShShader *s) { return s ? s->ird : ""; }
const char *sh_shader_ast(const ShShader *s) { return s ? s->astd : ""; }
uint64_t sh_shader_cycles(const ShShader *s, int stat) {
    if (!s || !s->c) {
        return 0;
    }
    if (stat == SH_STAT_LEX) {
        return s->c->cyc_lex;
    }
    if (stat == SH_STAT_PARSE) {
        return s->c->cyc_parse;
    }
    if (stat == SH_STAT_SEM) {
        return s->c->cyc_sem;
    }
    if (stat == SH_STAT_IR) {
        return s->c->cyc_ir;
    }
    if (stat == SH_STAT_TGSI) {
        return s->c->cyc_tgsi;
    }
    return 0;
}

int sh_live_count(void) { return g_live; }
int sh_cache_count(void) { return g_cache_n; }

ShProgram *sh_program_create(void) {
    ShProgram *p = (ShProgram *)sh_alloc(sizeof *p);
    int i;
    if (!p) {
        return 0;
    }
    memset(p, 0, sizeof *p);
    for (i = 0; i < 8; ++i) {
        p->var_remap[i] = -1;
    }
    p->gen = 1;
    g_live++;
    return p;
}

void sh_program_free(ShProgram *p) {
    if (!p) {
        return;
    }
    sh_free(p);
    if (g_live > 0) {
        g_live--;
    }
}

int sh_program_attach(ShProgram *p, ShShader *s) {
    if (!p || !s || !s->ok || !s->c) {
        return -1;
    }
    if (s->c->stage == SH_STAGE_VERTEX) {
        p->vs = s;
        return 0;
    }
    if (s->c->stage == SH_STAGE_FRAGMENT) {
        p->fs = s;
        return 0;
    }
    return -1;
}

static int find_uni(ShProgram *p, const char *name) {
    int i;
    for (i = 0; i < p->nuni; ++i) {
        if (sh_eq(p->uni[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static void add_uni(ShProgram *p, const Sym *s, int stage) {
    int idx;
    int nvec = s->ty == SH_TY_MAT4 ? 4 : s->ty == SH_TY_MAT3 ? 3 : 1;
    int tight = s->ty == SH_TY_MAT4 ? 64 : s->ty == SH_TY_MAT3 ? 36 : s->ty == SH_TY_VEC4 ? 16
                : s->ty == SH_TY_VEC3 || s->ty == SH_TY_IVEC3                              ? 12
                : s->ty == SH_TY_VEC2 || s->ty == SH_TY_IVEC2                              ? 8
                                                                                           : 4;
    if (s->ty == SH_TY_SAMPLER2D) {
        p->nsamp++;
        return;
    }
    idx = find_uni(p, s->name);
    if (idx < 0) {
        if (p->nuni >= 32) {
            prog_log(p, "too many uniforms");
            return;
        }
        idx = p->nuni++;
        memset(&p->uni[idx], 0, sizeof p->uni[idx]);
        {
            int k = 0;
            while (s->name[k] && k + 1 < (int)sizeof p->uni[idx].name) {
                p->uni[idx].name[k] = s->name[k];
                ++k;
            }
            p->uni[idx].name[k] = 0;
        }
        p->uni[idx].type = s->ty;
        p->uni[idx].stage = stage;
        p->uni[idx].slot = s->slot;
        p->uni[idx].offset = s->slot * 16;
        p->uni[idx].size = tight;
    } else if (p->uni[idx].type != s->ty) {
        prog_log(p, "uniform type mismatch");
    }
    (void)nvec;
}

static int link_into(ShProgram *p, char *vst, char *fst) {
    int i;
    int j;
    const ShComp *vs;
    const ShComp *fs;
    int map[8];
    if (!p->vs || !p->fs || !p->vs->ok || !p->fs->ok) {
        prog_log(p, "program is missing a compiled shader");
        return -1;
    }
    vs = p->vs->c;
    fs = p->fs->c;
    if (!vs->wrote_pos) {
        prog_log(p, "vertex shader does not write gl_Position");
        return -1;
    }
    if (fs->nout < 1) {
        prog_log(p, "fragment shader has no output");
        return -1;
    }
    for (i = 0; i < 8; ++i) {
        map[i] = -1;
    }
    p->nvary = 0;
    for (i = 0; i < fs->nsym; ++i) {
        const Sym *in = &fs->sym[i];
        int found = 0;
        if (in->kind != SYM_IN) {
            continue;
        }
        for (j = 0; j < vs->nsym; ++j) {
            const Sym *out = &vs->sym[j];
            if (out->kind != SYM_OUT || !sh_eq(out->name, in->name)) {
                continue;
            }
            found = 1;
            if (out->ty != in->ty) {
                prog_log(p, "type mismatch for varying");
                prog_log(p, in->name);
                return -1;
            }
            if (in->slot < 0 || in->slot >= 8 || out->slot < 0) {
                prog_log(p, "invalid varying slot");
                return -1;
            }
            map[in->slot] = out->slot;
            if (p->nvary < 8) {
                ShVaryInfo *v = &p->vary[p->nvary++];
                int k = 0;
                memset(v, 0, sizeof *v);
                while (in->name[k] && k + 1 < (int)sizeof v->name) {
                    v->name[k] = in->name[k];
                    ++k;
                }
                v->type = in->ty;
                v->slot = out->slot;
                v->stage = SH_STAGE_VERTEX;
            }
        }
        if (!found) {
            prog_log(p, "missing varying");
            prog_log(p, in->name);
            return -1;
        }
    }
    p->nattr = 0;
    for (i = 0; i < vs->nsym && p->nattr < 8; ++i) {
        const Sym *s = &vs->sym[i];
        if (s->kind != SYM_IN) {
            continue;
        }
        {
            ShAttribInfo *a = &p->attr[p->nattr++];
            int k = 0;
            memset(a, 0, sizeof *a);
            while (s->name[k] && k + 1 < (int)sizeof a->name) {
                a->name[k] = s->name[k];
                ++k;
            }
            a->type = s->ty;
            a->location = s->loc;
            a->ncomp = sh_ncomp_ty(s->ty);
        }
    }
    p->nuni = 0;
    p->nsamp = 0;
    for (i = 0; i < vs->nsym; ++i) {
        if (vs->sym[i].kind == SYM_UNIFORM) {
            add_uni(p, &vs->sym[i], SH_STAGE_VERTEX);
        }
    }
    for (i = 0; i < fs->nsym; ++i) {
        if (fs->sym[i].kind == SYM_UNIFORM) {
            add_uni(p, &fs->sym[i], SH_STAGE_FRAGMENT);
        }
    }
    if (p->logn && !p->linked) {
        return -1;
    }
    {
        int saved_n = p->fs->c->nerr;
        int saved_l = p->fs->c->logn;
        if (sh_emit_tgsi(p->vs->c, vst, SH_TGSI_MAX, 0) != 0 ||
            sh_emit_tgsi(p->fs->c, fst, SH_TGSI_MAX, map) != 0) {
            p->fs->c->nerr = saved_n;
            p->fs->c->logn = saved_l;
            if (saved_l < SH_LOG_MAX) {
                p->fs->c->log[saved_l] = 0;
            }
            p->vs->c->nerr = 0;
            prog_log(p, "TGSI emission failed");
            return -1;
        }
        p->vs->c->nerr = 0;
        p->fs->c->nerr = saved_n;
    }
    p->vs_nvec = p->vs->c->uni_hi + 1;
    p->fs_nvec = p->fs->c->uni_hi + 1;
    for (i = 0; i < 8; ++i) {
        p->var_remap[i] = map[i];
    }
    return 0;
}

int sh_program_link(ShProgram *p) {
    char vst[SH_TGSI_MAX];
    char fst[SH_TGSI_MAX];
    int was;
    if (!p) {
        return -1;
    }
    was = p->linked;
    p->logn = 0;
    p->log[0] = 0;
    if (link_into(p, vst, fst) != 0) {
        if (was) {
            prog_log(p, "kept previous program");
            p->linked = 1;
        }
        return -1;
    }
    memcpy(p->vs_tgsi, vst, SH_TGSI_MAX);
    memcpy(p->fs_tgsi, fst, SH_TGSI_MAX);
    p->linked = 1;
    p->gen++;
    if (p->gen == 0u) {
        p->gen = 1;
    }
    return 0;
}

int sh_program_ok(const ShProgram *p) { return p && p->linked; }
uint32_t sh_program_gen(const ShProgram *p) { return p ? p->gen : 0u; }
const char *sh_program_log(const ShProgram *p) { return p ? p->log : ""; }
const char *sh_program_tgsi(const ShProgram *p, int stage) {
    if (!p || !p->linked) {
        return "";
    }
    return stage == SH_STAGE_FRAGMENT ? p->fs_tgsi : p->vs_tgsi;
}

int sh_uniform_count(const ShProgram *p) { return p ? p->nuni : 0; }
int sh_uniform_info(const ShProgram *p, int index, ShUniformInfo *out) {
    if (!p || !out || index < 0 || index >= p->nuni) {
        return -1;
    }
    *out = p->uni[index];
    return 0;
}
int sh_uniform_find(const ShProgram *p, const char *name) {
    int i;
    if (!p || !name) {
        return -1;
    }
    for (i = 0; i < p->nuni; ++i) {
        if (sh_eq(p->uni[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static void write_uni(float *words, int slot, int ty, const float *v, int nfloat) {
    int i;
    if (ty == SH_TY_MAT4 && nfloat >= 16) {
        for (i = 0; i < 16; ++i) {
            words[slot * 4 + i] = v[i];
        }
        return;
    }
    if (ty == SH_TY_MAT3 && nfloat >= 9) {
        int col;
        for (col = 0; col < 3; ++col) {
            words[(slot + col) * 4 + 0] = v[col * 3 + 0];
            words[(slot + col) * 4 + 1] = v[col * 3 + 1];
            words[(slot + col) * 4 + 2] = v[col * 3 + 2];
            words[(slot + col) * 4 + 3] = 0.f;
        }
        return;
    }
    for (i = 0; i < nfloat && i < 4; ++i) {
        words[slot * 4 + i] = v[i];
    }
}

int sh_uniform_set(ShProgram *p, int loc, const float *v, int nfloat) {
    ShUniformInfo *u;
    const ShComp *vs;
    const ShComp *fs;
    int i;
    if (!p || !p->linked || !v || loc < 0 || loc >= p->nuni) {
        return -1;
    }
    u = &p->uni[loc];
    vs = p->vs->c;
    fs = p->fs->c;
    for (i = 0; i < vs->nsym; ++i) {
        if (vs->sym[i].kind == SYM_UNIFORM && sh_eq(vs->sym[i].name, u->name)) {
            write_uni(p->vs_words, vs->sym[i].slot, vs->sym[i].ty, v, nfloat);
        }
    }
    for (i = 0; i < fs->nsym; ++i) {
        if (fs->sym[i].kind == SYM_UNIFORM && sh_eq(fs->sym[i].name, u->name)) {
            write_uni(p->fs_words, fs->sym[i].slot, fs->sym[i].ty, v, nfloat);
        }
    }
    return 0;
}

int sh_uniform_set_lane(ShProgram *p, int loc, int lane, float v) {
    float tmp[16];
    int i;
    int n;
    if (!p || loc < 0 || loc >= p->nuni || lane < 0 || lane >= 16) {
        return -1;
    }
    n = p->uni[loc].type == SH_TY_MAT4 ? 16 : p->uni[loc].type == SH_TY_MAT3 ? 9 : 4;
    for (i = 0; i < 16; ++i) {
        tmp[i] = 0.f;
    }
    if (p->uni[loc].stage != SH_STAGE_FRAGMENT) {
        int slot = -1;
        int k;
        for (k = 0; k < p->vs->c->nsym; ++k) {
            if (p->vs->c->sym[k].kind == SYM_UNIFORM && sh_eq(p->vs->c->sym[k].name, p->uni[loc].name)) {
                slot = p->vs->c->sym[k].slot;
            }
        }
        if (slot >= 0 && p->uni[loc].type == SH_TY_MAT4) {
            for (i = 0; i < 16; ++i) {
                tmp[i] = p->vs_words[slot * 4 + i];
            }
        }
    }
    tmp[lane] = v;
    return sh_uniform_set(p, loc, tmp, n);
}

int sh_attrib_count(const ShProgram *p) { return p ? p->nattr : 0; }
int sh_attrib_info(const ShProgram *p, int index, ShAttribInfo *out) {
    if (!p || !out || index < 0 || index >= p->nattr) {
        return -1;
    }
    *out = p->attr[index];
    return 0;
}
int sh_vary_count(const ShProgram *p) { return p ? p->nvary : 0; }
int sh_vary_info(const ShProgram *p, int index, ShVaryInfo *out) {
    if (!p || !out || index < 0 || index >= p->nvary) {
        return -1;
    }
    *out = p->vary[index];
    return 0;
}
int sh_sampler_count(const ShProgram *p) { return p ? p->nsamp : 0; }
int sh_sampler_find(const ShProgram *p, const char *name) {
    int i;
    if (!p || !p->fs || !name) {
        return -1;
    }
    for (i = 0; i < p->fs->c->nsym; ++i) {
        if (p->fs->c->sym[i].kind == SYM_UNIFORM && p->fs->c->sym[i].ty == SH_TY_SAMPLER2D &&
            sh_eq(p->fs->c->sym[i].name, name)) {
            return p->fs->c->sym[i].slot;
        }
    }
    for (i = 0; i < p->vs->c->nsym; ++i) {
        if (p->vs->c->sym[i].kind == SYM_UNIFORM && p->vs->c->sym[i].ty == SH_TY_SAMPLER2D &&
            sh_eq(p->vs->c->sym[i].name, name)) {
            return p->vs->c->sym[i].slot;
        }
    }
    return -1;
}

const float *sh_uniform_words(const ShProgram *p, int stage, int *nvec) {
    if (!p) {
        return 0;
    }
    if (stage == SH_STAGE_FRAGMENT) {
        if (nvec) {
            *nvec = p->fs_nvec;
        }
        return p->fs_words;
    }
    if (nvec) {
        *nvec = p->vs_nvec;
    }
    return p->vs_words;
}

int sh_soft_vs(const ShProgram *p, const float *attr, float pos[4], float vary[][4]) {
    if (!p || !p->linked || !attr || !pos) {
        return -1;
    }
    return sh_exec_ir(p->vs->c, attr, p->vs_words, 0, 0, 0, 0, 0, pos, vary, 0, 0, 0);
}

int sh_soft_fs(const ShProgram *p, const float vary_in[][4], const float fragcoord[4],
               const uint8_t *tex, int tex_w, int tex_h, float color[4], int *discarded) {
    if (!p || !p->linked || !color) {
        return -1;
    }
    return sh_exec_ir(p->fs->c, 0, p->fs_words, vary_in ? &vary_in[0][0] : 0, fragcoord, tex,
                      tex_w, tex_h, 0, 0, color, discarded, p->var_remap);
}

static float edgef(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

int sh_soft_triangle(const ShProgram *p, const float *v0, const float *v1, const float *v2,
                     int attr_stride, const uint8_t *tex, int tex_w, int tex_h, uint32_t *pixels,
                     int w, int h) {
    float pos[3][4];
    float var[3][8][4];
    float x[3], y[3], invw[3];
    int i;
    int minx, miny, maxx, maxy;
    int ix, iy;
    float *zb = 0;
    if (!p || !p->linked || !pixels || w <= 0 || h <= 0 || w > 128 || h > 128 || attr_stride < 4) {
        return -1;
    }
    memset(var, 0, sizeof var);
    if (!v0 || !v1 || !v2) {
        return -1;
    }
    if (sh_soft_vs(p, v0, pos[0], var[0]) != 0 || sh_soft_vs(p, v1, pos[1], var[1]) != 0 ||
        sh_soft_vs(p, v2, pos[2], var[2]) != 0) {
        return -1;
    }
    (void)attr_stride;
    for (i = 0; i < 3; ++i) {
        if (pos[i][3] < 0.0001f && pos[i][3] > -0.0001f) {
            return -1;
        }
        invw[i] = 1.f / pos[i][3];
        x[i] = (pos[i][0] * invw[i] * 0.5f + 0.5f) * (float)w;
        y[i] = (1.f - (pos[i][1] * invw[i] * 0.5f + 0.5f)) * (float)h;
    }
    minx = (int)x[0];
    maxx = minx;
    miny = (int)y[0];
    maxy = miny;
    for (i = 1; i < 3; ++i) {
        int xi = (int)x[i];
        int yi = (int)y[i];
        if (xi < minx) {
            minx = xi;
        }
        if (yi < miny) {
            miny = yi;
        }
        if (xi > maxx) {
            maxx = xi;
        }
        if (yi > maxy) {
            maxy = yi;
        }
    }
    if (minx < 0) {
        minx = 0;
    }
    if (miny < 0) {
        miny = 0;
    }
    if (maxx >= w) {
        maxx = w - 1;
    }
    if (maxy >= h) {
        maxy = h - 1;
    }
    zb = (float *)sh_alloc((unsigned long)w * (unsigned long)h * sizeof(float));
    if (!zb) {
        return -1;
    }
    for (i = 0; i < w * h; ++i) {
        zb[i] = 1.f;
    }
    for (iy = miny; iy <= maxy; ++iy) {
        for (ix = minx; ix <= maxx; ++ix) {
            float px = (float)ix + 0.5f;
            float py = (float)iy + 0.5f;
            float den = edgef(x[0], y[0], x[1], y[1], x[2], y[2]);
            float b0, b1, b2, iw, z;
            float vin[8][4];
            float color[4];
            int discarded = 0;
            int slot;
            if (den > -0.0001f && den < 0.0001f) {
                continue;
            }
            b0 = edgef(x[1], y[1], x[2], y[2], px, py) / den;
            b1 = edgef(x[2], y[2], x[0], y[0], px, py) / den;
            b2 = edgef(x[0], y[0], x[1], y[1], px, py) / den;
            if (b0 < -0.001f || b1 < -0.001f || b2 < -0.001f) {
                continue;
            }
            iw = b0 * invw[0] + b1 * invw[1] + b2 * invw[2];
            if (iw == 0.f) {
                continue;
            }
            z = (b0 * pos[0][2] * invw[0] + b1 * pos[1][2] * invw[1] + b2 * pos[2][2] * invw[2]) / iw;
            if (z >= zb[iy * w + ix]) {
                continue;
            }
            memset(vin, 0, sizeof vin);
            for (slot = 0; slot < 8; ++slot) {
                int c0;
                int src = slot;
                for (c0 = 0; c0 < 4; ++c0) {
                    float num = b0 * var[0][src][c0] * invw[0] + b1 * var[1][src][c0] * invw[1] +
                                b2 * var[2][src][c0] * invw[2];
                    vin[slot][c0] = num / iw;
                }
            }
            {
                float fc[4];
                fc[0] = px;
                fc[1] = py;
                fc[2] = z;
                fc[3] = 1.f / iw;
                if (sh_soft_fs(p, vin, fc, tex, tex_w, tex_h, color, &discarded) != 0) {
                    sh_free(zb);
                    return -1;
                }
            }
            if (discarded) {
                continue;
            }
            {
                int r = (int)(color[0] * 255.f + 0.5f);
                int g = (int)(color[1] * 255.f + 0.5f);
                int b = (int)(color[2] * 255.f + 0.5f);
                int a = (int)(color[3] * 255.f + 0.5f);
                if (r < 0) {
                    r = 0;
                }
                if (g < 0) {
                    g = 0;
                }
                if (b < 0) {
                    b = 0;
                }
                if (a < 0) {
                    a = 0;
                }
                if (r > 255) {
                    r = 255;
                }
                if (g > 255) {
                    g = 255;
                }
                if (b > 255) {
                    b = 255;
                }
                if (a > 255) {
                    a = 255;
                }
                pixels[iy * w + ix] = (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) |
                                      ((uint32_t)a << 24);
                zb[iy * w + ix] = z;
            }
        }
    }
    sh_free(zb);
    return 0;
}

int sh_shader_save_csi(const ShShader *s, void *dst, int cap) {
    uint8_t *out = (uint8_t *)dst;
    uint32_t hdr[8];
    uint32_t sum = 0;
    int i;
    int need;
    if (!s || !s->ok || !s->c || !dst) {
        return -1;
    }
    need = 32 + s->c->nir * (int)sizeof(Ir) + s->c->nimm * (int)sizeof(float);
    if (cap < need) {
        return -1;
    }
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 0x52495343u;
    hdr[1] = 1;
    hdr[2] = (uint32_t)s->c->stage;
    hdr[3] = (uint32_t)s->c->nir;
    hdr[4] = (uint32_t)s->c->nimm;
    hdr[5] = (uint32_t)s->c->ntmp;
    for (i = 0; i < s->c->nir * (int)sizeof(Ir); ++i) {
        sum += ((const uint8_t *)s->c->ir)[i];
    }
    hdr[6] = sum;
    memcpy(out, hdr, 32);
    memcpy(out + 32, s->c->ir, (unsigned long)s->c->nir * sizeof(Ir));
    if (s->c->nimm) {
        memcpy(out + 32 + s->c->nir * (int)sizeof(Ir), s->c->imm,
               (unsigned long)s->c->nimm * sizeof(float));
    }
    return need;
}

ShShader *sh_shader_load_csi(const void *src, int len) {
    const uint8_t *in = (const uint8_t *)src;
    uint32_t hdr[8];
    uint32_t sum = 0;
    int i;
    int nir;
    int nimm;
    int need;
    ShShader *s;
    if (!src || len < 32) {
        return 0;
    }
    memcpy(hdr, in, 32);
    if (hdr[0] != 0x52495343u || hdr[1] != 1u) {
        return 0;
    }
    if (hdr[2] != SH_STAGE_VERTEX && hdr[2] != SH_STAGE_FRAGMENT) {
        return 0;
    }
    nir = (int)hdr[3];
    nimm = (int)hdr[4];
    if (nir < 0 || nir > SH_IR_MAX || nimm < 0 || nimm > SH_IMM_MAX || (int)hdr[5] < 0 ||
        (int)hdr[5] > SH_TEMP_MAX) {
        return 0;
    }
    need = 32 + nir * (int)sizeof(Ir) + nimm * (int)sizeof(float);
    if (len != need) {
        return 0;
    }
    s = (ShShader *)sh_alloc(sizeof *s);
    if (!s) {
        return 0;
    }
    memset(s, 0, sizeof *s);
    s->c = (ShComp *)sh_alloc(sizeof(ShComp));
    if (!s->c) {
        sh_free(s);
        return 0;
    }
    memset(s->c, 0, sizeof *s->c);
    g_live++;
    s->c->stage = (int)hdr[2];
    s->c->nir = nir;
    s->c->nimm = nimm;
    s->c->ntmp = (int)hdr[5];
    s->c->name[0] = 'c';
    s->c->name[1] = 's';
    s->c->name[2] = 'i';
    s->c->name[3] = 0;
    memcpy(s->c->ir, in + 32, (unsigned long)nir * sizeof(Ir));
    if (nimm) {
        memcpy(s->c->imm, in + 32 + nir * (int)sizeof(Ir), (unsigned long)nimm * sizeof(float));
    }
    for (i = 0; i < nir * (int)sizeof(Ir); ++i) {
        sum += (in + 32)[i];
    }
    if (sum != hdr[6] || sh_verify(s->c) != 0) {
        sh_shader_free(s);
        return 0;
    }
    if (sh_emit_tgsi(s->c, s->tgsi, SH_TGSI_MAX, 0) != 0) {
        s->ok = 0;
        return s;
    }
    s->ok = 1;
    sh_dump_ir_buf(s->c, s->ird, (int)sizeof s->ird);
    return s;
}

#define SH_G_SH 16
#define SH_G_PR 8
static ShShader *g_sh[SH_G_SH];
static int g_sh_own[SH_G_SH];
static ShProgram *g_pr[SH_G_PR];
static int g_pr_own[SH_G_PR];

int sh_guest_compile(int owner, int stage, const char *name, const char *src) {
    int i;
    ShShader *s = sh_compile(stage, name, src);
    if (!s) {
        return -1;
    }
    for (i = 0; i < SH_G_SH; ++i) {
        if (!g_sh[i]) {
            g_sh[i] = s;
            g_sh_own[i] = owner;
            return i + 1;
        }
    }
    sh_shader_free(s);
    return -1;
}

static ShShader *guest_sh(int owner, int id) {
    if (id < 1 || id > SH_G_SH) {
        return 0;
    }
    if (!g_sh[id - 1] || g_sh_own[id - 1] != owner) {
        return 0;
    }
    return g_sh[id - 1];
}

static ShProgram *guest_pr(int owner, int id) {
    if (id < 1 || id > SH_G_PR) {
        return 0;
    }
    if (!g_pr[id - 1] || g_pr_own[id - 1] != owner) {
        return 0;
    }
    return g_pr[id - 1];
}

int sh_guest_shader_ok(int owner, int id) {
    ShShader *s = guest_sh(owner, id);
    return s && s->ok ? 1 : 0;
}

int sh_guest_shader_log(int owner, int id, char *dst, int cap) {
    ShShader *s = guest_sh(owner, id);
    int i = 0;
    const char *log;
    if (!s || !dst || cap < 1) {
        return -1;
    }
    log = sh_shader_log(s);
    while (log[i] && i + 1 < cap) {
        dst[i] = log[i];
        ++i;
    }
    dst[i] = 0;
    return i;
}

int sh_guest_shader_drop(int owner, int id) {
    ShShader *s = guest_sh(owner, id);
    if (!s) {
        return -2;
    }
    sh_shader_free(s);
    g_sh[id - 1] = 0;
    return 0;
}

ShProgram *sh_guest_program(int owner, int id) {
    return guest_pr(owner, id);
}

int sh_guest_prog_make(int owner) {
    int i;
    ShProgram *p = sh_program_create();
    if (!p) {
        return -1;
    }
    for (i = 0; i < SH_G_PR; ++i) {
        if (!g_pr[i]) {
            g_pr[i] = p;
            g_pr_own[i] = owner;
            return i + 1;
        }
    }
    sh_program_free(p);
    return -1;
}

int sh_guest_prog_attach(int owner, int prog, int shader) {
    ShProgram *p = guest_pr(owner, prog);
    ShShader *s = guest_sh(owner, shader);
    if (!p || !s) {
        return -2;
    }
    return sh_program_attach(p, s);
}

int sh_guest_prog_link(int owner, int prog) {
    ShProgram *p = guest_pr(owner, prog);
    if (!p) {
        return -2;
    }
    return sh_program_link(p);
}

int sh_guest_prog_ok(int owner, int prog) {
    ShProgram *p = guest_pr(owner, prog);
    return p && p->linked ? 1 : 0;
}

int sh_guest_prog_log(int owner, int prog, char *dst, int cap) {
    ShProgram *p = guest_pr(owner, prog);
    int i = 0;
    if (!p || !dst || cap < 1) {
        return -1;
    }
    while (p->log[i] && i + 1 < cap) {
        dst[i] = p->log[i];
        ++i;
    }
    dst[i] = 0;
    return i;
}

int sh_guest_prog_drop(int owner, int prog) {
    ShProgram *p = guest_pr(owner, prog);
    if (!p) {
        return -2;
    }
    sh_program_free(p);
    g_pr[prog - 1] = 0;
    return 0;
}

int sh_guest_uniloc(int owner, int prog, const char *name) {
    ShProgram *p = guest_pr(owner, prog);
    if (!p) {
        return -2;
    }
    return sh_uniform_find(p, name);
}

int sh_guest_setf(int owner, int prog, int loc, int lane, float v) {
    ShProgram *p = guest_pr(owner, prog);
    if (!p) {
        return -2;
    }
    return sh_uniform_set_lane(p, loc, lane, v);
}

int sh_guest_samp(int owner, int prog, const char *name) {
    ShProgram *p = guest_pr(owner, prog);
    if (!p) {
        return -2;
    }
    return sh_sampler_find(p, name);
}

void sh_guest_drop_owner(int owner) {
    int i;
    for (i = 0; i < SH_G_SH; ++i) {
        if (g_sh[i] && g_sh_own[i] == owner) {
            sh_shader_free(g_sh[i]);
            g_sh[i] = 0;
        }
    }
    for (i = 0; i < SH_G_PR; ++i) {
        if (g_pr[i] && g_pr_own[i] == owner) {
            sh_program_free(g_pr[i]);
            g_pr[i] = 0;
        }
    }
}
