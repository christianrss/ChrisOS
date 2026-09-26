#include "virgl_cmd.h"
#include "virgl_proto.h"
#include "virtgpu_enc.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "virgl: %s\n", msg);
    return 1;
}

int main(void) {
    uint32_t d[64];
    uint8_t raw[128];
    uint32_t len = 0;
    VirglCmd cmd;
    VgpuCreate3D info;
    uint32_t tiny[4];
    VirglCmd bad;
    if (virgl_cmd_init(&cmd, d, 0, 1) == 0) {
        return fail("zero cap");
    }
    if (virgl_cmd_init(&bad, tiny, 2, 1) != 0) {
        return fail("tiny init");
    }
    if (virgl_cmd_clear(&bad, 4, 1, 2, 3, 4, 0, 0) == 0) {
        return fail("overflow");
    }
    memset(d, 0, sizeof d);
    if (virgl_cmd_init(&cmd, d, 64, 3) != 0 ||
        virgl_cmd_clear(&cmd, 4u, 0x3f800000u, 0, 0, 0x3f800000u, 0, 0) != 0) {
        return fail("clear");
    }
    if (d[0] != VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, 8) || d[1] != 4u || cmd.n != 9u) {
        return fail("clear words");
    }
    if (virgl_cmd_surface(&cmd, 0, 4, 1) == 0) {
        return fail("bad handle");
    }
    info.resource_id = 0;
    info.target = 2;
    info.format = 1;
    info.bind = 2;
    info.width = 0;
    info.height = 8;
    info.depth = 1;
    info.array_size = 1;
    info.last_level = 0;
    info.nr_samples = 0;
    info.flags = 1;
    if (vgpu_enc_create_3d(raw, sizeof raw, &info, &len) == 0) {
        return fail("zero width");
    }
    info.width = 16;
    info.resource_id = 9;
    if (vgpu_enc_create_3d(raw, sizeof raw, &info, &len) != 0 || len != 72u) {
        return fail("create3d");
    }
    if (vgpu_rd32(raw, 0) != VGPU_CMD_RESOURCE_CREATE_3D || vgpu_rd32(raw, 24) != 9u ||
        vgpu_rd32(raw, 28) != 2u) {
        return fail("create3d words");
    }
    if (vgpu_enc_ctx_create(raw, sizeof raw, 4, 7, 2, "chrisos", &len) != 0 ||
        vgpu_rd32(raw, 16) != 4u || vgpu_rd32(raw, 28) != 2u) {
        return fail("ctx");
    }
    if (vgpu_enc_simple(raw, sizeof raw, VGPU_CMD_CTX_ATTACH_RESOURCE, 4, 9, &len) != 0 ||
        vgpu_rd32(raw, 0) != VGPU_CMD_CTX_ATTACH_RESOURCE || vgpu_rd32(raw, 16) != 4u ||
        vgpu_rd32(raw, 24) != 9u) {
        return fail("attach");
    }
    {
        uint32_t words[2];
        words[0] = 1;
        words[1] = 2;
        if (vgpu_enc_submit3d(raw, 16, 4, words, 2, &len) == 0) {
            return fail("submit overflow");
        }
        if (vgpu_enc_submit3d(raw, sizeof raw, 4, words, 2, &len) != 0 ||
            vgpu_rd32(raw, 0) != VGPU_CMD_SUBMIT_3D || vgpu_rd32(raw, 24) != 8u ||
            vgpu_rd32(raw, 32) != 1u) {
            return fail("submit");
        }
    }
    if (vgpu_enc_create_2d(raw, sizeof raw, 3, 1, 8, 8, &len) != 0 || len != 40u) {
        return fail("create2d");
    }
    if (virgl_cmd_init(&cmd, d, 64, 3) != 0 || virgl_cmd_destroy(&cmd, VIRGL_OBJECT_SHADER, 9u) != 0) {
        return fail("destroy");
    }
    if (d[0] != VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT, VIRGL_OBJECT_SHADER, 1) || d[1] != 9u) {
        return fail("destroy words");
    }
    printf("test_virgl_cmd: ok\n");
    return 0;
}
