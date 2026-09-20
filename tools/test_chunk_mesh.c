#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "math3d.h"
#include "voxel.h"
#include "zbuf.h"

int main(void) {
    static uint32_t pix[320 * 200];
    int painted = 0;
    int x;
    int z;

    memset(pix, 0, sizeof(pix));
    math3d_set_screen(320, 200);
    math3d_cam_set(12.0f, 6.0f, 22.0f, 0.0f, 0.0f);
    zbuf_set_size(320, 200);
    zbuf_clear();
    for (x = 0; x < 24; ++x) {
        for (z = 0; z < 24; ++z)
            assert(voxel_set(x, 0, z, 1) == 0);
    }
    assert(voxel_set(8, 1, 8, 2) == 0);
    assert(voxel_set(8, 2, 8, 4) == 0);
    assert(voxel_get(8, 1, 8) == 2);
    assert(voxel_world_draw(pix, 320, 200) == 0);
    for (x = 0; x < 320 * 200; ++x) {
        if (pix[x] != 0)
            painted++;
    }
    assert(painted > 500);
    puts("test_chunk_mesh: ok");
    return 0;
}
