#ifndef CHRIS_CLA_H
#define CHRIS_CLA_H

#include <stddef.h>
#include <stdint.h>
#include "../il/il.h"

#define CLA_MAGIC 0x00414C43u /* 'CLA\0' little */
#define CLA_MAX_ASM 8
#define CLA_NAME_MAX 32

typedef struct ClaMethod {
    char name[CLA_NAME_MAX];
    uint32_t rva;
    uint32_t size;
    IlSig sig;
} ClaMethod;

typedef struct ClaType {
    char name[CLA_NAME_MAX];
    uint32_t size;
    uint32_t gc_bits;
    uint16_t nfields;
} ClaType;

typedef struct ClaImage {
    char name[CLA_NAME_MAX];
    uint16_t version;
    uint16_t nmethods;
    uint16_t ntypes;
    uint16_t nrefs;
    ClaMethod methods[64];
    ClaType types[32];
    char refs[8][CLA_NAME_MAX];
    const uint8_t *il;
    uint32_t il_size;
    uint8_t loaded;
} ClaImage;

int cla_parse(const uint8_t *file, size_t n, ClaImage *out);
int cla_write(uint8_t *out, size_t cap, const ClaImage *img);
int cla_resolve(ClaImage *img, ClaImage **tab, int ntab);
int cla_load_bytes(const uint8_t *file, size_t n);
int cla_count(void);
ClaImage *cla_get(int i);
void cla_reset(void);

#endif
