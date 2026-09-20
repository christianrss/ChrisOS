/* Tiny IWAD with PLAYPAL so Doom bring-up has a 256-color palette. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *f;
    uint8_t pal[768];
    uint8_t dir[16];
    const char *out = "GAMES/DOOM/DOOM1.WAD";
    int i;
    uint32_t pos;
    if (argc > 1)
        out = argv[1];
    for (i = 0; i < 256; i++) {
        pal[i * 3] = (uint8_t)i;
        pal[i * 3 + 1] = (uint8_t)((i * 3) / 4);
        pal[i * 3 + 2] = (uint8_t)(i / 2);
    }
    f = fopen(out, "wb");
    if (!f)
        return 1;
    fwrite("IWAD", 1, 4, f);
    i = 1;
    fwrite(&i, 4, 1, f);
    pos = 12 + 768;
    fwrite(&pos, 4, 1, f);
    fwrite(pal, 1, 768, f);
    memset(dir, 0, 16);
    pos = 12;
    memcpy(dir, &pos, 4);
    pos = 768;
    memcpy(dir + 4, &pos, 4);
    memcpy(dir + 8, "PLAYPAL", 7);
    fwrite(dir, 1, 16, f);
    fclose(f);
    return 0;
}
