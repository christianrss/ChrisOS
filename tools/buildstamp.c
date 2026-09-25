#include "sha256.h"

#include <string.h>

#define BUILDSTAMP_TAG "CHRISOSHASH:"
#define BUILDSTAMP_HEX 64

static int is_hex(char value) {
    if (value >= '0' && value <= '9') {
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        return 1;
    }
    return 0;
}

int buildstamp_seal(uint8_t *image, uint32_t n) {
    uint32_t i;
    uint32_t at;
    uint8_t digest[32];
    static const char digits[] = "0123456789abcdef";
    const uint32_t tag_len = 12u;

    if (!image || n < tag_len + BUILDSTAMP_HEX) {
        return -1;
    }
    at = n;
    for (i = 0u; i + tag_len + BUILDSTAMP_HEX <= n; i++) {
        if (memcmp(image + i, BUILDSTAMP_TAG, tag_len) != 0) {
            continue;
        }
        at = i + tag_len;
        break;
    }
    if (at == n) {
        return -1;
    }
    for (i = 0u; i < BUILDSTAMP_HEX; i++) {
        if (!is_hex((char)image[at + i])) {
            return -1;
        }
        image[at + i] = '0';
    }
    sha256(image, n, digest);
    for (i = 0u; i < 32u; i++) {
        image[at + i * 2u] = (uint8_t)digits[digest[i] >> 4];
        image[at + i * 2u + 1u] = (uint8_t)digits[digest[i] & 0x0fu];
    }
    return 0;
}
