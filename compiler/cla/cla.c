#include "cla.h"
#include "../il/il.h"

static ClaImage g_asm[CLA_MAX_ASM];
static uint8_t g_ilstore[CLA_MAX_ASM][65536];
static int g_nasm;

void cla_reset(void) {
    g_nasm = 0;
}

int cla_count(void) {
    return g_nasm;
}

ClaImage *cla_get(int i) {
    if (i < 0 || i >= g_nasm)
        return 0;
    return &g_asm[i];
}

int cla_load_bytes(const uint8_t *file, size_t n) {
    ClaImage img;
    ClaImage *tab[CLA_MAX_ASM];
    uint32_t i;
    uint32_t k;
    if (!cla_parse(file, n, &img))
        return 0;
    if (g_nasm >= CLA_MAX_ASM || img.il_size > 65536u)
        return 0;
    for (i = 0; i < img.nmethods; ++i) {
        uint32_t r = img.methods[i].rva;
        uint32_t s = img.methods[i].size;
        if (r + s > img.il_size)
            return 0;
        if (!il_verify(img.il + r, s, &img.methods[i].sig))
            return 0;
    }
    for (i = 0; i < (uint32_t)g_nasm; ++i)
        tab[i] = &g_asm[i];
    if (img.nrefs && !cla_resolve(&img, tab, g_nasm))
        return 0;
    g_asm[g_nasm] = img;
    for (k = 0; k < img.il_size; ++k)
        g_ilstore[g_nasm][k] = img.il[k];
    g_asm[g_nasm].il = g_ilstore[g_nasm];
    g_nasm++;
    return 1;
}

static void cpy(char *d, size_t n, const char *s) {
    size_t i = 0;
    if (!n)
        return;
    while (s && s[i] && i + 1 < n) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}

static uint32_t ru32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wu32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int cla_parse(const uint8_t *file, size_t n, ClaImage *out) {
    uint32_t magic;
    uint32_t i;
    uint32_t off;
    if (!file || !out || n < 48)
        return 0;
    magic = ru32(file);
    if (magic != CLA_MAGIC)
        return 0;
    out->version = (uint16_t)(file[4] | (file[5] << 8));
    out->nmethods = (uint16_t)(file[6] | (file[7] << 8));
    out->ntypes = (uint16_t)(file[8] | (file[9] << 8));
    out->nrefs = (uint16_t)(file[10] | (file[11] << 8));
    if (out->nmethods > 64 || out->ntypes > 32 || out->nrefs > 8)
        return 0;
    for (i = 0; i < CLA_NAME_MAX; ++i)
        out->name[i] = (char)file[12 + i];
    out->name[CLA_NAME_MAX - 1] = 0;
    off = 48;
    for (i = 0; i < out->nmethods; ++i) {
        if (off + 48 > n)
            return 0;
        cpy(out->methods[i].name, CLA_NAME_MAX, (const char *)file + off);
        out->methods[i].rva = ru32(file + off + 32);
        out->methods[i].size = ru32(file + off + 36);
        out->methods[i].sig.ret = (IlType)file[off + 40];
        out->methods[i].sig.argc = file[off + 41];
        off += 48;
    }
    for (i = 0; i < out->ntypes; ++i) {
        if (off + 40 > n)
            return 0;
        cpy(out->types[i].name, CLA_NAME_MAX, (const char *)file + off);
        out->types[i].size = ru32(file + off + 32);
        out->types[i].gc_bits = ru32(file + off + 36);
        off += 40;
    }
    for (i = 0; i < out->nrefs; ++i) {
        if (off + CLA_NAME_MAX > n)
            return 0;
        cpy(out->refs[i], CLA_NAME_MAX, (const char *)file + off);
        off += CLA_NAME_MAX;
    }
    if (off > n)
        return 0;
    out->il = file + off;
    out->il_size = (uint32_t)(n - off);
    out->loaded = 1;
    return 1;
}

int cla_write(uint8_t *out, size_t cap, const ClaImage *img) {
    size_t need;
    uint32_t i;
    uint32_t off;
    if (!out || !img)
        return 0;
    need = 48u + (uint32_t)img->nmethods * 48u + (uint32_t)img->ntypes * 40u +
           (uint32_t)img->nrefs * CLA_NAME_MAX + img->il_size;
    if (need > cap)
        return 0;
    wu32(out, CLA_MAGIC);
    out[4] = (uint8_t)img->version;
    out[5] = (uint8_t)(img->version >> 8);
    out[6] = (uint8_t)img->nmethods;
    out[7] = (uint8_t)(img->nmethods >> 8);
    out[8] = (uint8_t)img->ntypes;
    out[9] = (uint8_t)(img->ntypes >> 8);
    out[10] = (uint8_t)img->nrefs;
    out[11] = (uint8_t)(img->nrefs >> 8);
    for (i = 0; i < CLA_NAME_MAX; ++i)
        out[12 + i] = (uint8_t)img->name[i];
    off = 48;
    for (i = 0; i < img->nmethods; ++i) {
        uint32_t k;
        for (k = 0; k < 48; ++k)
            out[off + k] = 0;
        for (k = 0; k < CLA_NAME_MAX - 1 && img->methods[i].name[k]; ++k)
            out[off + k] = (uint8_t)img->methods[i].name[k];
        wu32(out + off + 32, img->methods[i].rva);
        wu32(out + off + 36, img->methods[i].size);
        out[off + 40] = (uint8_t)img->methods[i].sig.ret;
        out[off + 41] = img->methods[i].sig.argc;
        off += 48;
    }
    for (i = 0; i < img->ntypes; ++i) {
        uint32_t k;
        for (k = 0; k < 40; ++k)
            out[off + k] = 0;
        for (k = 0; k < CLA_NAME_MAX - 1 && img->types[i].name[k]; ++k)
            out[off + k] = (uint8_t)img->types[i].name[k];
        wu32(out + off + 32, img->types[i].size);
        wu32(out + off + 36, img->types[i].gc_bits);
        off += 40;
    }
    for (i = 0; i < img->nrefs; ++i) {
        uint32_t k;
        for (k = 0; k < CLA_NAME_MAX; ++k)
            out[off + k] = 0;
        for (k = 0; k < CLA_NAME_MAX - 1 && img->refs[i][k]; ++k)
            out[off + k] = (uint8_t)img->refs[i][k];
        off += CLA_NAME_MAX;
    }
    if (img->il && img->il_size) {
        uint32_t k;
        for (k = 0; k < img->il_size; ++k)
            out[off + k] = img->il[k];
    }
    return (int)need;
}

int cla_resolve(ClaImage *img, ClaImage **tab, int ntab) {
    int r;
    int t;
    if (!img)
        return 0;
    for (r = 0; r < img->nrefs; ++r) {
        int found = 0;
        for (t = 0; t < ntab; ++t) {
            if (tab[t] && tab[t]->name[0]) {
                int k = 0;
                while (img->refs[r][k] && img->refs[r][k] == tab[t]->name[k])
                    ++k;
                if (img->refs[r][k] == 0 && tab[t]->name[k] == 0)
                    found = 1;
            }
        }
        if (!found)
            return 0;
    }
    return 1;
}
