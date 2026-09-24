/* LEARN:SH16-12 */
#include "chriso.h"
#include <string.h>

#define CHRISO_HDR 64u
#define CHRISO_SYM_BYTES 80u
#define CHRISO_REL_V1 16u
#define CHRISO_REL_V2 20u

void chriso_init(ChrisoImage *img) {
    memset(img, 0, sizeof(*img));
}

static uint32_t chriso_file_bytes(const ChrisoImage *img) {
    uint32_t need = CHRISO_HDR;
    uint32_t i;

    for (i = 0; i < CHRISO_SEC_BSS; i++) {
        need += img->sec_size[i];
    }
    need += img->nsym * CHRISO_SYM_BYTES;
    need += img->nrel * CHRISO_REL_V2;
    return need;
}

int chriso_write(const ChrisoImage *img, void *out, uint32_t cap) {
    uint32_t need;
    uint32_t off;
    uint32_t i;
    uint32_t *w;

    if (!img || !out) {
        return -1;
    }
    need = chriso_file_bytes(img);
    if (cap < need) {
        return -1;
    }
    memset(out, 0, need);
    w = (uint32_t *)out;
    w[0] = CHRISO_MAGIC;
    w[1] = CHRISO_VERSION;
    w[2] = img->nsym;
    w[3] = img->nrel;
    w[4] = img->sec_size[CHRISO_SEC_TEXT];
    w[5] = img->sec_size[CHRISO_SEC_RODATA];
    w[6] = img->sec_size[CHRISO_SEC_DATA];
    w[7] = img->sec_size[CHRISO_SEC_BSS];
    off = CHRISO_HDR;
    for (i = 0; i < CHRISO_SEC_BSS; i++) {
        if (img->sec_size[i] > 0 && img->sec[i]) {
            memcpy((uint8_t *)out + off, img->sec[i], img->sec_size[i]);
            off += img->sec_size[i];
        }
    }
    for (i = 0; i < img->nsym; i++) {
        memcpy((uint8_t *)out + off, &img->sym[i], CHRISO_SYM_BYTES);
        off += CHRISO_SYM_BYTES;
    }
    for (i = 0; i < img->nrel; i++) {
        memcpy((uint8_t *)out + off, &img->rel[i], CHRISO_REL_V2);
        off += CHRISO_REL_V2;
    }
    return (int)need;
}

int chriso_read(ChrisoImage *img, const void *in, uint32_t n) {
    const uint32_t *w = in;
    const uint8_t *raw = in;
    uint32_t off;
    uint32_t i;
    uint32_t need;
    uint32_t ver;
    uint32_t rel_bytes;

    if (!img || !in || n < CHRISO_HDR || w[0] != CHRISO_MAGIC) {
        return -1;
    }
    ver = w[1];
    if (ver != CHRISO_VERSION_V1 && ver != CHRISO_VERSION) {
        return -1;
    }
    chriso_init(img);
    img->nsym = w[2];
    img->nrel = w[3];
    if (img->nsym > CHRISO_SYM_MAX || img->nrel > CHRISO_REL_MAX) {
        return -1;
    }
    img->sec_size[CHRISO_SEC_TEXT] = w[4];
    img->sec_size[CHRISO_SEC_RODATA] = w[5];
    img->sec_size[CHRISO_SEC_DATA] = w[6];
    img->sec_size[CHRISO_SEC_BSS] = ver >= CHRISO_VERSION ? w[7] : 0u;
    rel_bytes = ver >= CHRISO_VERSION ? CHRISO_REL_V2 : CHRISO_REL_V1;
    need = CHRISO_HDR;
    for (i = 0; i < CHRISO_SEC_BSS; i++) {
        need += img->sec_size[i];
    }
    need += img->nsym * CHRISO_SYM_BYTES;
    need += img->nrel * rel_bytes;
    if (n < need) {
        return -1;
    }
    off = CHRISO_HDR;
    for (i = 0; i < CHRISO_SEC_BSS; i++) {
        if (img->sec_size[i] > 0) {
            img->sec[i] = (uint8_t *)(raw + off);
            off += img->sec_size[i];
        }
    }
    for (i = 0; i < img->nsym; i++) {
        memcpy(&img->sym[i], raw + off, CHRISO_SYM_BYTES);
        if (ver == CHRISO_VERSION_V1) {
            img->sym[i].binding = CHRISO_BIND_LOCAL;
            img->sym[i].kind = CHRISO_KIND_NOTYPE;
            img->sym[i].reserved = 0;
        }
        off += CHRISO_SYM_BYTES;
    }
    for (i = 0; i < img->nrel; i++) {
        memset(&img->rel[i], 0, sizeof(img->rel[i]));
        memcpy(&img->rel[i], raw + off, rel_bytes);
        off += rel_bytes;
    }
    return 0;
}

int chriso_merge_text(ChrisoImage *dst, const ChrisoImage *src) {
    uint32_t off;
    uint32_t i;

    if (!dst || !src) {
        return -1;
    }
    off = dst->sec_size[CHRISO_SEC_TEXT];
    if (off + src->sec_size[CHRISO_SEC_TEXT] > 65536u) {
        return -1;
    }
    if (!dst->sec[CHRISO_SEC_TEXT]) {
        return -1;
    }
    if (src->sec_size[CHRISO_SEC_TEXT] > 0 && src->sec[CHRISO_SEC_TEXT]) {
        memcpy(dst->sec[CHRISO_SEC_TEXT] + off,
               src->sec[CHRISO_SEC_TEXT],
               src->sec_size[CHRISO_SEC_TEXT]);
        dst->sec_size[CHRISO_SEC_TEXT] = off + src->sec_size[CHRISO_SEC_TEXT];
    }
    for (i = 0; i < src->nsym; i++) {
        if (dst->nsym >= CHRISO_SYM_MAX) {
            return -1;
        }
        dst->sym[dst->nsym] = src->sym[i];
        if (src->sym[i].binding != CHRISO_BIND_UNDEF &&
            src->sym[i].section == CHRISO_SEC_TEXT) {
            dst->sym[dst->nsym].offset += off;
        }
        dst->nsym++;
    }
    for (i = 0; i < src->nrel; i++) {
        if (dst->nrel >= CHRISO_REL_MAX) {
            return -1;
        }
        dst->rel[dst->nrel] = src->rel[i];
        if (src->rel[i].section == CHRISO_SEC_TEXT) {
            dst->rel[dst->nrel].offset += off;
        }
        dst->rel[dst->nrel].sym_index += dst->nsym - src->nsym;
        dst->nrel++;
    }
    return 0;
}
