#ifndef CHRIS_CHRISC_H
#define CHRIS_CHRISC_H
#include <stddef.h>
#include <stdint.h>

typedef struct ChrisDiag {
    int line, column;
    char message[80];
} ChrisDiag;

typedef struct ChrisResult {
    size_t code_size;
    uint16_t entry;
    unsigned variables;
    ChrisDiag diag;
} ChrisResult;

int chrisc_compile(const char *source, size_t source_size,
                   uint8_t *code, size_t code_cap, ChrisResult *result);
#endif
