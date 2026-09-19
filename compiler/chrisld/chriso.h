/* LEARN:SH16-12 */
#ifndef CHRIS_CHRISO_H
#define CHRIS_CHRISO_H

#include <stdint.h>

#define CHRISO_MAGIC 0x4F524843u
#define CHRISO_VERSION 1u
#define CHRISO_SEC_TEXT 0u
#define CHRISO_SEC_RODATA 1u
#define CHRISO_SEC_DATA 2u
#define CHRISO_SEC_MAX 3u
#define CHRISO_SYM_MAX 256u
#define CHRISO_REL_MAX 512u

typedef struct ChrisoSym {
    char name[64];
    uint32_t section;
    uint32_t offset;
    uint32_t size;
} ChrisoSym;

typedef struct ChrisoRel {
    uint32_t section;
    uint32_t offset;
    uint32_t sym_index;
    int32_t addend;
} ChrisoRel;

typedef struct ChrisoImage {
    uint8_t *sec[CHRISO_SEC_MAX];
    uint32_t sec_size[CHRISO_SEC_MAX];
    ChrisoSym sym[CHRISO_SYM_MAX];
    uint32_t nsym;
    ChrisoRel rel[CHRISO_REL_MAX];
    uint32_t nrel;
} ChrisoImage;

void chriso_init(ChrisoImage *img);
int chriso_write(const ChrisoImage *img, void *out, uint32_t cap);
int chriso_read(ChrisoImage *img, const void *in, uint32_t n);
int chriso_merge_text(ChrisoImage *dst, const ChrisoImage *src);

#endif
