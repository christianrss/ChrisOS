#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../compiler/clvm/clvm.h"

int main(void) {
    static const uint8_t code[] = { CL_OP_PUSH, 42, 0, 0, 0, CL_OP_HALT };
    uint8_t file[64];
    ClvmImage image;
    size_t size = clvm_write_image(file, sizeof(file), CLVM_FLAG_GAME,
                                   0, code, sizeof(code));
    assert(size == CLVM_HEADER_SIZE + sizeof(code));
    assert(clvm_parse(file, size, &image) == CL_LOAD_OK);
    assert(image.flags == CLVM_FLAG_GAME);
    assert(image.entry == 0);
    assert(image.code_size == sizeof(code));
    assert(memcmp(image.code, code, sizeof(code)) == 0);

    file[0] = 'X';
    assert(clvm_parse(file, size, &image) == CL_LOAD_MAGIC);
    file[0] = 'C';
    file[8] = 99;
    assert(clvm_parse(file, size, &image) == CL_LOAD_SIZE);
    file[8] = (uint8_t)sizeof(code);
    file[size - 1] ^= 1;
    assert(clvm_parse(file, size, &image) == CL_LOAD_CHECKSUM);

    puts("test_clvm_format: ok");
    return 0;
}
