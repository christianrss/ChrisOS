#ifndef CHRIS_VIRTGPU_ENC_H
#define CHRIS_VIRTGPU_ENC_H

#include <stdint.h>

#define VGPU_CMD_GET_DISPLAY_INFO 0x0100u
#define VGPU_CMD_RESOURCE_CREATE_2D 0x0101u
#define VGPU_CMD_RESOURCE_UNREF 0x0102u
#define VGPU_CMD_SET_SCANOUT 0x0103u
#define VGPU_CMD_RESOURCE_FLUSH 0x0104u
#define VGPU_CMD_TRANSFER_TO_HOST_2D 0x0105u
#define VGPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VGPU_CMD_RESOURCE_DETACH_BACKING 0x0107u
#define VGPU_CMD_GET_CAPSET_INFO 0x0108u
#define VGPU_CMD_GET_CAPSET 0x0109u
#define VGPU_CMD_CTX_CREATE 0x0200u
#define VGPU_CMD_CTX_DESTROY 0x0201u
#define VGPU_CMD_CTX_ATTACH_RESOURCE 0x0202u
#define VGPU_CMD_CTX_DETACH_RESOURCE 0x0203u
#define VGPU_CMD_RESOURCE_CREATE_3D 0x0204u
#define VGPU_CMD_TRANSFER_TO_HOST_3D 0x0205u
#define VGPU_CMD_TRANSFER_FROM_HOST_3D 0x0206u
#define VGPU_CMD_SUBMIT_3D 0x0207u
#define VGPU_CMD_UPDATE_CURSOR 0x0300u
#define VGPU_CMD_MOVE_CURSOR 0x0301u

#define VGPU_RESP_OK_NODATA 0x1100u
#define VGPU_RESP_OK_DISPLAY_INFO 0x1101u
#define VGPU_RESP_OK_CAPSET_INFO 0x1102u
#define VGPU_RESP_OK_CAPSET 0x1103u
#define VGPU_RESP_ERR_UNSPEC 0x1200u

#define VGPU_FLAG_FENCE 1u
#define VGPU_HDR_SIZE 24u
#define VGPU_CAPSET_VIRGL 1u
#define VGPU_CAPSET_VIRGL2 2u
#define VGPU_FORMAT_B8G8R8A8 1u
#define VGPU_F_VIRGL 0u
#define VGPU_F_EDID 1u
#define VGPU_F_RESOURCE_UUID 2u
#define VGPU_F_RESOURCE_BLOB 3u
#define VGPU_F_CONTEXT_INIT 4u

typedef struct VgpuCreate3D {
    uint32_t resource_id;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
} VgpuCreate3D;

typedef struct VgpuXfer3D {
    uint32_t resource_id;
    uint32_t x, y, z, w, h, d;
    uint64_t offset;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
} VgpuXfer3D;

void vgpu_wr32(uint8_t *p, uint32_t off, uint32_t v);
uint32_t vgpu_rd32(const uint8_t *p, uint32_t off);
void vgpu_hdr(uint8_t *p, uint32_t type, uint32_t flags, uint64_t fence, uint32_t ctx);

int vgpu_enc_simple(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t ctx,
                    uint32_t id, uint32_t *out_len);
int vgpu_enc_create_2d(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t fmt,
                       uint32_t w, uint32_t h, uint32_t *out_len);
int vgpu_enc_attach(uint8_t *dst, uint32_t cap, uint32_t id, uint64_t addr,
                    uint32_t length, uint32_t *out_len);
int vgpu_enc_scanout(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t scanout,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint32_t *out_len);
int vgpu_enc_flush(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t x,
                   uint32_t y, uint32_t w, uint32_t h, uint32_t *out_len);
int vgpu_enc_xfer2d(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t x,
                    uint32_t y, uint32_t w, uint32_t h, uint64_t off,
                    uint32_t *out_len);
int vgpu_enc_create_3d(uint8_t *dst, uint32_t cap, const VgpuCreate3D *info,
                       uint32_t *out_len);
int vgpu_enc_xfer3d(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t ctx,
                    const VgpuXfer3D *box, uint32_t *out_len);
int vgpu_enc_ctx_create(uint8_t *dst, uint32_t cap, uint32_t ctx, uint32_t nlen,
                        uint32_t context_init, const char *name,
                        uint32_t *out_len);
int vgpu_enc_submit3d(uint8_t *dst, uint32_t cap, uint32_t ctx,
                      const uint32_t *cmds, uint32_t ndwords, uint32_t *out_len);
int vgpu_enc_capset_info(uint8_t *dst, uint32_t cap, uint32_t index,
                         uint32_t *out_len);
int vgpu_enc_capset(uint8_t *dst, uint32_t cap, uint32_t id, uint32_t version,
                    uint32_t *out_len);
int vgpu_enc_cursor(uint8_t *dst, uint32_t cap, uint32_t type, uint32_t res,
                    uint32_t x, uint32_t y, uint32_t hot_x, uint32_t hot_y,
                    uint32_t *out_len);

#endif
