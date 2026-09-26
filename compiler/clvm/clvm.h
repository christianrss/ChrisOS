#ifndef CHRIS_CLVM_H
#define CHRIS_CLVM_H

#include <stddef.h>
#include <stdint.h>

#define CLVM_VERSION 2u
#define CLVM_VERSION_V1 1u
#define CLVM_HEADER_SIZE 16u
#define CLVM_HEADER_SIZE_V2 24u
#define CLVM_MAX_CODE (16u * 1024u * 1024u)
#define CLVM_FLAG_GAME 0x01u
#define CLVM_KNOWN_FLAGS CLVM_FLAG_GAME

enum ClvmOpcode {
    CL_OP_NOP   = 0x00,
    CL_OP_PUSH  = 0x01,
    CL_OP_ADD   = 0x02,
    CL_OP_SUB   = 0x03,
    CL_OP_MUL   = 0x04,
    CL_OP_DIV   = 0x05,
    CL_OP_DUP   = 0x06,
    CL_OP_PRINT = 0x07,
    CL_OP_HALT  = 0x08,
    CL_OP_JMP   = 0x09,
    CL_OP_JZ    = 0x0a,
    CL_OP_CALL  = 0x0b,
    CL_OP_RET   = 0x0c,
    CL_OP_LOAD  = 0x0d,
    CL_OP_STORE = 0x0e,
    CL_OP_DROP  = 0x0f,
    CL_OP_SWAP  = 0x10,
    CL_OP_EQ    = 0x11,
    CL_OP_LT    = 0x12,
    CL_OP_JNZ   = 0x13,
    CL_OP_MOD   = 0x14,
    CL_OP_NE    = 0x15,
    CL_OP_LE    = 0x16,
    CL_OP_GT    = 0x17,
    CL_OP_GE    = 0x18,
    CL_OP_NEG   = 0x19,
    CL_OP_LOADB = 0x1a,
    CL_OP_STOREB = 0x1b,
    CL_OP_CALLI = 0x1c,
    CL_OP_UDIV  = 0x1d,
    CL_OP_UMOD  = 0x1e,
    CL_OP_ULT   = 0x1f,
    CL_OP_SYS   = 0x20,
    CL_OP_JMP32 = 0x21,
    CL_OP_JZ32  = 0x22,
    CL_OP_JNZ32 = 0x23,
    CL_OP_CALL32 = 0x24,
    CL_OP_PUSH64 = 0x25,
    CL_OP_LOAD64 = 0x26,
    CL_OP_STORE64 = 0x27,
    CL_OP_FLOAD = 0x28,
    CL_OP_FSTORE = 0x29,
    CL_OP_FPUSH = 0x2a,
    CL_OP_FADD  = 0x2b,
    CL_OP_FSUB  = 0x2c,
    CL_OP_FMUL  = 0x2d,
    CL_OP_FDIV  = 0x2e,
    CL_OP_FNEG  = 0x2f,
    CL_OP_FTOI  = 0x30,
    CL_OP_ITOF  = 0x31,
    CL_OP_FEQ   = 0x32,
    CL_OP_FLT   = 0x33,
    CL_OP_FLE   = 0x34,
    CL_OP_AND   = 0x35,
    CL_OP_OR    = 0x36,
    CL_OP_XOR   = 0x37,
    CL_OP_SHL   = 0x38,
    CL_OP_SHR   = 0x39,
    CL_OP_SAR   = 0x3a,
    CL_OP_NOT   = 0x3b,
    CL_OP_LDARG = 0x3c,
    CL_OP_STLOC = 0x3d,
    CL_OP_LDLOC = 0x3e,
    CL_OP_NEWOBJ = 0x3f,
    CL_OP_LDFLD = 0x40,
    CL_OP_STFLD = 0x41,
    CL_OP_CALLT = 0x42,
    CL_OP_LDSTR = 0x43,
    CL_OP_SAFEPOINT = 0x44,
    CL_OP_ULE = 0x45,
    CL_OP_UGT = 0x46,
    CL_OP_UGE = 0x47
};

typedef enum ClvmLoadError {
    CL_LOAD_OK = 0,
    CL_LOAD_NULL,
    CL_LOAD_SMALL,
    CL_LOAD_MAGIC,
    CL_LOAD_VERSION,
    CL_LOAD_FLAGS,
    CL_LOAD_SIZE,
    CL_LOAD_ENTRY,
    CL_LOAD_CHECKSUM,
    CL_LOAD_OUTPUT
} ClvmLoadError;

typedef struct ClvmImage {
    uint8_t version;
    uint8_t flags;
    uint32_t entry;
    uint32_t code_size;
    uint32_t checksum;
    uint32_t mem_hint;
    uint32_t header_size;
    const uint8_t *code;
} ClvmImage;

uint32_t clvm_fnv1a32(const uint8_t *data, size_t size);
const char *clvm_load_error(ClvmLoadError error);
ClvmLoadError clvm_parse(const uint8_t *file, size_t file_size,
                         ClvmImage *image);
size_t clvm_write_image(uint8_t *out, size_t out_cap, uint8_t flags,
                        uint16_t entry, const uint8_t *code,
                        size_t code_size);
size_t clvm_write_image_v2(uint8_t *out, size_t out_cap, uint8_t flags,
                           uint32_t entry, uint32_t mem_hint,
                           const uint8_t *code, size_t code_size);

#endif
