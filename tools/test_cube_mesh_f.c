#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "clvm_vm.h"
#include "mesh.h"
#include "zbuf.h"

static uint8_t dummy_code[1] = { 0 };

static void wr_f32(uint8_t *mem, uint32_t off, float f) {
    union {
        float fv;
        uint32_t u;
    } v;
    v.fv = f;
    mem[off] = (uint8_t)(v.u & 0xffu);
    mem[off + 1] = (uint8_t)((v.u >> 8) & 0xffu);
    mem[off + 2] = (uint8_t)((v.u >> 16) & 0xffu);
    mem[off + 3] = (uint8_t)((v.u >> 24) & 0xffu);
}

static void wr_i32(uint8_t *mem, uint32_t off, int32_t n) {
    uint32_t u = (uint32_t)n;
    mem[off] = (uint8_t)(u & 0xffu);
    mem[off + 1] = (uint8_t)((u >> 8) & 0xffu);
    mem[off + 2] = (uint8_t)((u >> 16) & 0xffu);
    mem[off + 3] = (uint8_t)((u >> 24) & 0xffu);
}

static void cube_init(uint8_t *mem) {
    int i;
    static const float vx[8] = { -1, 1, 1, -1, -1, 1, 1, -1 };
    static const float vy[8] = { -1, -1, 1, 1, -1, -1, 1, 1 };
    static const float vz[8] = { -1, -1, -1, -1, 1, 1, 1, 1 };
    static const int32_t tri[36] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
        3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2
    };

    for (i = 0; i < 8; ++i) {
        wr_f32(mem, (uint32_t)i * 12u, vx[i]);
        wr_f32(mem, (uint32_t)i * 12u + 4u, vy[i]);
        wr_f32(mem, (uint32_t)i * 12u + 8u, vz[i]);
    }
    for (i = 0; i < 36; ++i)
        wr_i32(mem, 96u + (uint32_t)i * 4u, tri[i]);
}

int main(void) {
    ClvmVm vm;
    ClvmImage image;
    static uint32_t pix[320 * 200];
    int painted = 0;
    int cols = 0;
    int x;

    memset(&vm, 0, sizeof(vm));
    image.version = 1;
    image.flags = 1;
    image.entry = 0;
    image.code_size = 1;
    image.checksum = 0;
    image.code = dummy_code;
    clvm_vm_init(&vm, &image, 0, 0);
    cube_init(vm.memory);
    memset(pix, 0, sizeof(pix));
    zbuf_set_size(320, 200);
    zbuf_clear();
    assert(mesh_draw_f(&vm, 0, 8, 12, 0.0f, 0.0f, 0.0f, 25.0f, 12, pix, 320, 200) == 0);
    for (x = 0; x < 320 * 200; ++x) {
        if (pix[x] != 0)
            painted++;
    }
    for (x = 0; x < 320; ++x) {
        int y;
        for (y = 0; y < 200; ++y) {
            if (pix[y * 320 + x] != 0) {
                cols++;
                break;
            }
        }
    }
    assert(painted > 2000);
    assert(cols > 20);
    puts("test_cube_mesh_f: ok");
    return 0;
}
