#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int buildstamp_seal(uint8_t *image, uint32_t n);

int main(int argc, char **argv) {
    FILE *f;
    long size;
    uint8_t *image;
    size_t nread;

    if (argc != 2) {
        fprintf(stderr, "usage: stamp_kernel KERNEL.ELF\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }
    size = ftell(f);
    if (size < 0 || size > 0x7fffffffL) {
        fclose(f);
        fprintf(stderr, "stamp_kernel: size\n");
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 1;
    }
    image = (uint8_t *)malloc((size_t)size);
    if (!image) {
        fclose(f);
        return 1;
    }
    nread = fread(image, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(image);
        fprintf(stderr, "stamp_kernel: short read\n");
        return 1;
    }
    if (buildstamp_seal(image, (uint32_t)size) != 0) {
        free(image);
        fprintf(stderr, "stamp_kernel: identity slot missing\n");
        return 1;
    }
    f = fopen(argv[1], "wb");
    if (!f) {
        free(image);
        perror(argv[1]);
        return 1;
    }
    if (fwrite(image, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(image);
        fprintf(stderr, "stamp_kernel: short write\n");
        return 1;
    }
    fclose(f);
    free(image);
    printf("stamp_kernel: sha256 written\n");
    return 0;
}
