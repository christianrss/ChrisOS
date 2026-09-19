#include "chriso.h"
#include <stdio.h>
#include <stdint.h>

int main(void) {
    ChrisoImage img;
    uint8_t buf[64];

    chriso_init(&img);
    if (chriso_write(&img, buf, sizeof(buf)) < 64) {
        fprintf(stderr, "chriso_write failed\n");
        return 1;
    }
    chriso_init(&img);
    if (chriso_read(&img, buf, sizeof(buf)) != 0) {
        fprintf(stderr, "chriso_read failed\n");
        return 1;
    }
    if (((const uint32_t *)buf)[0] != CHRISO_MAGIC) {
        fprintf(stderr, "bad magic\n");
        return 1;
    }
    puts("test_chriso: ok");
    return 0;
}
