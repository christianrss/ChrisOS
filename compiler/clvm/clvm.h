#ifndef CHRIS_CLVM_H
#define CHRIS_CLVM_H

#include <stddef.h>
#include <stdint.h>

#define CLVM_VERSION 1u
#define CLVM_HEADER_SIZE 16u
#define CLVM_MAX_CODE 65535u
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
    CL_OP_SYS   = 0x20
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
    uint16_t entry;
    uint32_t code_size;
    uint32_t checksum;
    const uint8_t *code;
} ClvmImage;

uint32_t clvm_fnv1a32(const uint8_t *data, size_t size);
const char *clvm_load_error(ClvmLoadError error);
ClvmLoadError clvm_parse(const uint8_t *file, size_t file_size,
                         ClvmImage *image);
size_t clvm_write_image(uint8_t *out, size_t out_cap, uint8_t flags,
                        uint16_t entry, const uint8_t *code,
                        size_t code_size);

#endif
