#include "clvm.h"

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void wr32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

uint32_t clvm_fnv1a32(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

const char *clvm_load_error(ClvmLoadError error) {
    static const char *const text[] = {
        "ok", "null argument", "file smaller than header", "bad CLVM magic",
        "unsupported CLVM version", "unsupported CLVM flags",
        "invalid bytecode size", "entry outside bytecode",
        "bytecode checksum mismatch", "output buffer too small"
    };
    unsigned index = (unsigned)error;
    if (index >= sizeof(text) / sizeof(text[0]))
        return "unknown CLVM load error";
    return text[index];
}

ClvmLoadError clvm_parse(const uint8_t *file, size_t file_size,
                         ClvmImage *image) {
    ClvmImage value;
    if (file == NULL || image == NULL)
        return CL_LOAD_NULL;
    if (file_size < CLVM_HEADER_SIZE)
        return CL_LOAD_SMALL;
    if (file[0] != 'C' || file[1] != 'L' ||
        file[2] != 'V' || file[3] != 'M')
        return CL_LOAD_MAGIC;

    value.version = file[4];
    value.flags = file[5];
    value.entry = rd16(file + 6);
    value.code_size = rd32(file + 8);
    value.checksum = rd32(file + 12);
    value.code = file + CLVM_HEADER_SIZE;

    if (value.version != CLVM_VERSION)
        return CL_LOAD_VERSION;
    if ((value.flags & (uint8_t)~CLVM_KNOWN_FLAGS) != 0)
        return CL_LOAD_FLAGS;
    if (value.code_size == 0 || value.code_size > CLVM_MAX_CODE ||
        (size_t)value.code_size != file_size - CLVM_HEADER_SIZE)
        return CL_LOAD_SIZE;
    if (value.entry >= value.code_size)
        return CL_LOAD_ENTRY;
    if (clvm_fnv1a32(value.code, value.code_size) != value.checksum)
        return CL_LOAD_CHECKSUM;

    *image = value;
    return CL_LOAD_OK;
}

size_t clvm_write_image(uint8_t *out, size_t out_cap, uint8_t flags,
                        uint16_t entry, const uint8_t *code,
                        size_t code_size) {
    size_t i;
    if (out == NULL || code == NULL || code_size == 0 ||
        code_size > CLVM_MAX_CODE || entry >= code_size ||
        (flags & (uint8_t)~CLVM_KNOWN_FLAGS) != 0 ||
        out_cap < CLVM_HEADER_SIZE + code_size)
        return 0;

    out[0] = 'C'; out[1] = 'L'; out[2] = 'V'; out[3] = 'M';
    out[4] = CLVM_VERSION;
    out[5] = flags;
    wr16(out + 6, entry);
    wr32(out + 8, (uint32_t)code_size);
    wr32(out + 12, clvm_fnv1a32(code, code_size));
    for (i = 0; i < code_size; ++i)
        out[CLVM_HEADER_SIZE + i] = code[i];
    return CLVM_HEADER_SIZE + code_size;
}
