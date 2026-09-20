#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gfx2d.h"
#include "tri.h"
#include "zbuf.h"

#define W 32
#define H 32

static uint32_t pix[W * H];

int main(void) {
    zbuf_set_size(W, H);
    zbuf_clear();
    memset(pix, 0, sizeof(pix));

    {
        int n = 0;
        int i;
        tri_fill(pix, W, H, 2, 2, 500, 20, 2, 500, 2, 20, 500, 12);
        assert(pix[8 * W + 8] == 0xFF0000u);
        for (i = 0; i < W * H; ++i)
            if (pix[i] == 0xFF0000u)
                ++n;
        assert(n > 80);
    }

    tri_fill(pix, W, H, 4, 4, 100, 18, 4, 100, 4, 18, 100, 10);
    assert(pix[8 * W + 8] == 0x00FF00u);

    {
        enum { TW = 40, TH = 40 };
        uint32_t big[TW * TH];
        int n = 0;
        int cols = 0;
        int x;
        int y;
        int i;
        memset(big, 0, sizeof(big));
        zbuf_set_size(TW, TH);
        zbuf_clear();
        tri_fill(big, TW, TH, 2, 2, 500, 38, 2, 500, 2, 38, 500, 12);
        for (i = 0; i < TW * TH; ++i)
            if (big[i] == 0xFF0000u)
                ++n;
        assert(n > 400);
        for (x = 0; x < TW; ++x) {
            for (y = 0; y < TH; ++y) {
                if (big[y * TW + x] == 0xFF0000u) {
                    cols++;
                    break;
                }
            }
        }
        assert(cols > 8);
    }

    puts("test_tri: ok");
    return 0;
}
