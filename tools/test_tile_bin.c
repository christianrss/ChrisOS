#include <assert.h>
#include <stdio.h>

#include "tri_bin.h"

int main(void) {
    TriBin bin;
    int i;
    int empty = 0;

    tri_bin_reset(&bin, 640, 480);
    tri_bin_add(&bin, 10, 10, 20, 20, 0);
    tri_bin_add(&bin, 600, 400, 610, 410, 1);
    assert(bin.count > 0);

    for (i = 0; i < bin.count; ++i) {
        uint32_t e = bin.entries[i];
        int tx = (int)(e & 0xffu);
        int ty = (int)((e >> 8) & 0xffu);
        if (tx < 0 || ty < 0)
            empty++;
    }
    assert(empty == 0);
    puts("test_tile_bin: ok");
    return 0;
}
