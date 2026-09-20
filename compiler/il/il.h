#ifndef CHRIS_IL_H
#define CHRIS_IL_H

#include <stddef.h>
#include <stdint.h>

typedef enum IlType {
    IL_VOID = 0,
    IL_I4,
    IL_I8,
    IL_F4,
    IL_F8,
    IL_PTR,
    IL_REF
} IlType;

typedef struct IlSig {
    IlType ret;
    uint8_t argc;
    IlType args[8];
} IlSig;

int il_verify(const uint8_t *code, uint32_t size, const IlSig *sig);

#endif
