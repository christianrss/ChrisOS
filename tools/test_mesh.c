#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clvm_vm.h"
#include "math3d.h"
#include "mesh.h"
#include "zbuf.h"

static uint8_t dummy_code[1] = { 0 };

int main(void) {
    ClvmVm vm;
    ClvmImage image;
    static uint32_t pix[64 * 64];
    int32_t *mem;
    int x;
    int painted;

    memset(&vm, 0, sizeof(vm));
    image.version = 1;
    image.flags = 1;
    image.entry = 0;
    image.code_size = 1;
    image.checksum = 0;
    image.code = dummy_code;
    clvm_vm_init(&vm, &image, 0, 0);

    mem = (int32_t *)vm.memory;
    mem[0] = -1000;
    mem[1] = -1000;
    mem[2] = -1000;
    mem[3] = 1000;
    mem[4] = -1000;
    mem[5] = -1000;
    mem[6] = 1000;
    mem[7] = 1000;
    mem[8] = -1000;
    mem[9] = 0;
    mem[10] = 1;
    mem[11] = 2;

    memset(pix, 0, sizeof(pix));
    math3d_set_screen(64, 64);
    zbuf_set_size(64, 64);
    zbuf_clear();
    assert(mesh_draw(&vm, 0, 3, 1, 0, 12, pix, 64, 64) == 0);
    painted = 0;
    for (x = 0; x < 64 * 64; ++x)
        if (pix[x] != 0)
            painted = 1;
    assert(painted == 1);
    puts("test_mesh: ok");
    return 0;
}
