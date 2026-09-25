#ifndef CHRIS_VGPU_H
#define CHRIS_VGPU_H

#include <stdint.h>

#include "virtgpu_enc.h"

#define VGPU_OWNER_KERNEL 0

int vgpu_boot(int width, int height);
int vgpu_ready(void);
int vgpu_virgl_on(void);
int vgpu_cursor_active(void);
void vgpu_cursor_move(int x, int y);
void vgpu_flush_rect(int x, int y, int w, int h);
void vgpu_on_irq(void);

int vgpu_submit(const uint8_t *cmd, uint32_t cmd_len, uint32_t expect_resp,
                uint64_t fence, uint32_t *resp_type, uint32_t *used_len);
const uint8_t *vgpu_resp(void);
uint32_t vgpu_resp_cap(void);

int vgpu_res_create_2d(int owner, uint32_t fmt, uint32_t w, uint32_t h, int dma,
                       uint32_t backing, uint32_t *id);
int vgpu_res_create_3d(int owner, const VgpuCreate3D *info, int dma,
                       uint32_t backing, uint32_t *id);
int vgpu_res_attach(int owner, uint32_t id);
int vgpu_res_detach(int owner, uint32_t id);
int vgpu_res_unref(int owner, uint32_t id);
int vgpu_res_drop(int owner, uint32_t id);
int vgpu_res_flush(uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int vgpu_res_xfer2d(uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint64_t off);
int vgpu_set_scanout(uint32_t scanout, uint32_t id, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h);
int vgpu_xfer3d(int to_host, uint32_t ctx, const VgpuXfer3D *box);

int vgpu_ctx_create(int owner, uint32_t capset, int context_init, uint32_t *ctx);
int vgpu_ctx_destroy(int owner, uint32_t ctx);
int vgpu_ctx_attach(uint32_t ctx, uint32_t res);
int vgpu_ctx_detach(uint32_t ctx, uint32_t res);
int vgpu_submit3d(uint32_t ctx, const uint32_t *dwords, uint32_t ndwords);

int vgpu_debug(void);
uint32_t vgpu_primary_res(void);
int vgpu_res_live_count(void);
int vgpu_ctx_live_count(void);
void vgpu_fb_size(uint32_t *w, uint32_t *h);
uint32_t vgpu_capset_n(void);
int vgpu_capset_at(uint32_t index, uint32_t *id, uint32_t *ver, uint32_t *size,
                   const uint8_t **data);
uint64_t vgpu_dma_phys(int dma);
int vgpu_alloc_dma(int pages);
void vgpu_free_dma(int dma);
uint8_t *vgpu_dma_ptr(int dma);

#endif
