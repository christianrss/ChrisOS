/* LEARN:SH16-12 */
#include "chriso.h"
#include <string.h>

#define CHRISO_HDR 64u
#define CHRISO_SYM_BYTES 80u
#define CHRISO_REL_BYTES 16u

void chriso_init(ChrisoImage *img) {
    memset(img, 0, sizeof(*img));
}

static uint32_t chriso_payload_size(const ChrisoImage *img) {
    uint32_t need = CHRISO_HDR;
    uint32_t i;

    for (i = 0; i < CHRISO_SEC_MAX; i++) {
        need += img->sec_size[i];
    }
    need += img->nsym * CHRISO_SYM_BYTES;
    need += img->nrel * CHRISO_REL_BYTES;
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
    need = chriso_payload_size(img);
    if (cap < need) {
        return -1;
    }
    memset(out, 0, need);
    w = (uint32_t *)out;
    w[0] = CHRISO_MAGIC;
    w[1] = CHRISO_VERSION;
    w[2] = img->nsym;
    w[3] = img->nrel;
    for (i = 0; i < CHRISO_SEC_MAX; i++) {
        w[4 + i] = img->sec_size[i];
    }
    off = CHRISO_HDR;
    for (i = 0; i < CHRISO_SEC_MAX; i++) {
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
        memcpy((uint8_t *)out + off, &img->rel[i], CHRISO_REL_BYTES);
        off += CHRISO_REL_BYTES;
    }
    return (int)need;
}

int chriso_read(ChrisoImage *img, const void *in, uint32_t n) {
    const uint32_t *w = in;
    const uint8_t *raw = in;
    uint32_t off;
    uint32_t i;
    uint32_t need;

    if (!img || !in || n < CHRISO_HDR || w[0] != CHRISO_MAGIC) {
        return -1;
    }
    chriso_init(img);
    img->nsym = w[2];
    img->nrel = w[3];
    if (img->nsym > CHRISO_SYM_MAX || img->nrel > CHRISO_REL_MAX) {
        return -1;
    }
    for (i = 0; i < CHRISO_SEC_MAX; i++) {
        img->sec_size[i] = w[4 + i];
    }
    need = chriso_payload_size(img);
    if (n < need) {
        return -1;
    }
    off = CHRISO_HDR;
    for (i = 0; i < CHRISO_SEC_MAX; i++) {
        if (img->sec_size[i] > 0) {
            img->sec[i] = (uint8_t *)(raw + off);
            off += img->sec_size[i];
        }
    }
    for (i = 0; i < img->nsym; i++) {
        memcpy(&img->sym[i], raw + off, CHRISO_SYM_BYTES);
        off += CHRISO_SYM_BYTES;
    }
    for (i = 0; i < img->nrel; i++) {
        memcpy(&img->rel[i], raw + off, CHRISO_REL_BYTES);
        off += CHRISO_REL_BYTES;
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
        dst->sym[dst->nsym].offset += off;
        dst->nsym++;
    }
    return 0;
}
