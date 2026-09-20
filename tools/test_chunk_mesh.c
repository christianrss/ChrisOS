#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "math3d.h"
#include "voxel.h"
#include "zbuf.h"

static int count_painted(const uint32_t *pix) {
    int n = 0;
    int i;
    for (i = 0; i < 320 * 200; ++i) {
        if (pix[i] != 0)
            n++;
    }
    return n;
}

static int count_unique(const uint32_t *pix) {
    uint32_t seen[64];
    int n = 0;
    int i;
    int s;
    for (i = 0; i < 320 * 200; ++i) {
        uint32_t c = pix[i];
        int found = 0;
        if (c == 0)
            continue;
        for (s = 0; s < n; ++s) {
            if (seen[s] == c) {
                found = 1;
                break;
            }
        }
        if (!found && n < 64) {
            seen[n] = c;
            n++;
        }
    }
    return n;
}

int main(void) {
    static uint32_t pix[320 * 200];
    int painted;
    int painted2;
    int rebuilds;
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
    painted = count_painted(pix);
    assert(painted > 500);
    assert(count_unique(pix) > 8);
    rebuilds = voxel_mesh_rebuilds();
    assert(rebuilds > 0);

    memset(pix, 0, sizeof(pix));
    zbuf_clear();
    assert(voxel_world_draw(pix, 320, 200) == 0);
    painted2 = count_painted(pix);
    assert(painted2 > 500);
    assert(voxel_mesh_rebuilds() == rebuilds);

    puts("test_chunk_mesh: ok");
    return 0;
}
