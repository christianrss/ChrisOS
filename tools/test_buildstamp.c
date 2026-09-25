#include <stdio.h>
#include <string.h>

#include "sha256.h"

int buildstamp_seal(uint8_t *image, uint32_t n);

static void hex_of(const uint8_t digest[32], char out[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < 32u; i++) {
        out[i * 2u] = digits[digest[i] >> 4];
        out[i * 2u + 1u] = digits[digest[i] & 0x0fu];
    }
    out[64] = 0;
}

int main(void) {
    uint8_t image[128];
    uint8_t zeroed[128];
    uint8_t digest[32];
    char expect[65];

    memset(image, 0x11, sizeof image);
    memcpy(image + 8, "CHRISOSHASH:", 12);
    memset(image + 20, '0', 64);
    memcpy(zeroed, image, sizeof image);
    if (buildstamp_seal(image, sizeof image) != 0) {
        fprintf(stderr, "seal failed\n");
        return 1;
    }
    sha256(zeroed, sizeof zeroed, digest);
    hex_of(digest, expect);
    if (memcmp(image + 20, expect, 64) != 0) {
        fprintf(stderr, "hash mismatch\n");
        return 1;
    }
    if (memcmp(image, zeroed, 20) != 0 ||
        memcmp(image + 84, zeroed + 84, sizeof image - 84) != 0) {
        fprintf(stderr, "stamp wrote outside the slot\n");
        return 1;
    }
    memset(image, 0x22, sizeof image);
    if (buildstamp_seal(image, sizeof image) == 0) {
        fprintf(stderr, "missing tag sealed\n");
        return 1;
    }
    printf("build stamp tests passed\n");
    return 0;
}
