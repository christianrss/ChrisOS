/* LEARN:SH16-12 */
#ifndef CHRIS_CHRISO_H
#define CHRIS_CHRISO_H

#include <stdint.h>

#define CHRISO_MAGIC 0x4F524843u
#define CHRISO_VERSION 2u
#define CHRISO_VERSION_V1 1u
#define CHRISO_SEC_TEXT 0u
#define CHRISO_SEC_RODATA 1u
#define CHRISO_SEC_DATA 2u
#define CHRISO_SEC_BSS 3u
#define CHRISO_SEC_MAX 4u
#define CHRISO_SYM_MAX 256u
#define CHRISO_REL_MAX 512u

#define CHRISO_BIND_LOCAL 0u
#define CHRISO_BIND_GLOBAL 1u
#define CHRISO_BIND_UNDEF 2u
#define CHRISO_KIND_NOTYPE 0u
#define CHRISO_KIND_FUNC 1u
#define CHRISO_KIND_OBJECT 2u

#define R_X86_64_NONE 0u
#define R_X86_64_64 1u
#define R_X86_64_PC32 2u
#define R_X86_64_PLT32 4u
#define R_X86_64_32 10u
#define R_X86_64_32S 11u

typedef struct ChrisoSym {
    char name[64];
    uint32_t section;
    uint32_t offset;
    uint32_t size;
    uint8_t binding;
    uint8_t kind;
    uint16_t reserved;
} ChrisoSym;

typedef struct ChrisoRel {
    uint32_t section;
    uint32_t offset;
    uint32_t sym_index;
    int32_t addend;
    uint32_t type;
} ChrisoRel;

typedef struct ChrisoImage {
    uint8_t *sec[CHRISO_SEC_MAX];
    uint32_t sec_size[CHRISO_SEC_MAX];
    ChrisoSym sym[CHRISO_SYM_MAX];
    uint32_t nsym;
    ChrisoRel rel[CHRISO_REL_MAX];
    uint32_t nrel;
} ChrisoImage;

_Static_assert(sizeof(ChrisoSym) == 80, "ChrisoSym on-disk size");
_Static_assert(sizeof(ChrisoRel) == 20, "ChrisoRel v2 on-disk size");

void chriso_init(ChrisoImage *img);
int chriso_write(const ChrisoImage *img, void *out, uint32_t cap);
int chriso_read(ChrisoImage *img, const void *in, uint32_t n);
int chriso_merge_text(ChrisoImage *dst, const ChrisoImage *src);

#endif
