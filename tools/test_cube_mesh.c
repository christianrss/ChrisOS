#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clvm_vm.h"
#include "mesh.h"
#include "zbuf.h"

static uint8_t dummy_code[1] = { 0 };

static void wr_i32(uint8_t *mem, uint32_t off, int32_t v) {
    uint32_t u = (uint32_t)v;
    mem[off] = (uint8_t)(u & 0xffu);
    mem[off + 1] = (uint8_t)((u >> 8) & 0xffu);
    mem[off + 2] = (uint8_t)((u >> 16) & 0xffu);
    mem[off + 3] = (uint8_t)((u >> 24) & 0xffu);
}

static void cube_init(uint8_t *mem) {
    int one = 1000;
    int i;
    static const int32_t vx[8] = {-1, 1, 1, -1, -1, 1, 1, -1};
    static const int32_t vy[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
    static const int32_t vz[8] = {-1, -1, -1, -1, 1, 1, 1, 1};
    static const int32_t tri[36] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
        3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2
    };

    for (i = 0; i < 8; ++i) {
        wr_i32(mem, (uint32_t)i * 12u, vx[i] * one);
        wr_i32(mem, (uint32_t)i * 12u + 4u, vy[i] * one);
        wr_i32(mem, (uint32_t)i * 12u + 8u, vz[i] * one);
    }
    for (i = 0; i < 36; ++i)
        wr_i32(mem, 96u + (uint32_t)i * 4u, tri[i]);
}

int main(void) {
    ClvmVm vm;
    ClvmImage image;
    static uint32_t pix[320 * 200];
    int painted = 0;
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
    assert(mesh_draw(&vm, 0, 8, 12, 45, 14, pix, 320, 200) == 0);
    for (x = 0; x < 320 * 200; ++x) {
        if (pix[x] != 0)
            painted++;
    }
    assert(painted > 2000);
    puts("test_cube_mesh: ok");
    return 0;
}
