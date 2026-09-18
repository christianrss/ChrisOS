#ifndef CHRIS_CLASM_H
#define CHRIS_CLASM_H

#include <stddef.h>
#include <stdint.h>

#define CLASM_MAX_SOURCE 32768u
#define CLASM_MAX_LABELS 128
#define CLASM_NAME_MAX 24

typedef struct ClasmDiag {
    int line;
    int column;
    char message[64];
} ClasmDiag;

typedef struct ClasmResult {
    size_t code_size;
    uint16_t entry;
    ClasmDiag diag;
} ClasmResult;

int clasm_compile(const char *source, size_t source_size,
                  uint8_t *code, size_t code_cap, ClasmResult *result);

#endif
