#include "vgpu.h"

#include "bootinfo.h"
#include "gpures.h"
#include "graphics.h"
#include "hwgate.h"
#include "irq.h"
#include "pci.h"
#include "serial.h"
#include "virtq.h"
#include "gfx3d.h"
#include "virgl_demo.h"

#define VGPU_QSZ 16
#define VGPU_SPIN_FAST 2000000u
#define VGPU_SPIN_SLOW 80000000u
#define VGPU_TSC_FAST 2000000000ull
#define VGPU_TSC_SLOW 20000000000ull

typedef struct VqBind {
    Virtq q;
    int dma;
    int notify_win;
    uint32_t notify_off;
    int index;
    int ready;
} VqBind;

static VqBind g_ctrl;
static VqBind g_cursor;
static GpuPool g_pool;
static int g_online;
static int g_live;
static int g_dead;
static int g_busy;
static int g_cmd_dma = -1;
static int g_resp_dma = -1;
static int g_fb_dma = -1;
static int g_cur_dma = -1;
static uint32_t g_fb_w;
static uint32_t g_fb_h;
static uint32_t g_scan_res;
static uint32_t g_cur_res;
static int g_cursor_on;
static int g_cur_x;
static int g_cur_y;
static uint32_t g_cur_gen;
static uint32_t g_cur_sent;
static irq_handler g_irq_prev;
static int g_cwin;
static uint32_t g_coff;
static int g_isr_win;
static uint32_t g_isr_off;
static int g_isr_ok;
static uint8_t g_irq_line;
static uint32_t g_irq_count;
static uint64_t g_fence_seq = 1;
static uint32_t g_dev_lo;
static uint32_t g_dev_hi;
static uint32_t g_req_lo;
static uint32_t g_req_hi;
static uint32_t g_neg_lo;
static uint32_t g_neg_hi;
static uint32_t g_num_capsets;
static uint32_t g_num_scanouts;
static uint32_t g_cap_n;
static uint32_t g_rects;
static uint32_t g_pixels;
static uint32_t g_bytes;
static uint32_t g_full;
static uint32_t g_partial;
static uint32_t g_waits;

#define VGPU_CAP_BYTES 8192u
#define VGPU_CAP_MAX 4u
static struct {
    uint32_t id;
    uint32_t ver;
    uint32_t size;
    uint8_t data[VGPU_CAP_BYTES];
} g_cap[VGPU_CAP_MAX];

int vgpu_ready(void) {
    return g_live && !g_dead;
}

int vgpu_virgl_on(void) {
    return (g_neg_lo & (1u << VGPU_F_VIRGL)) != 0u && g_cap_n > 0u;
}

int vgpu_cursor_active(void) {
    return g_cursor_on;
}

int vgpu_debug(void) {
    return bootflag_gfx_debug();
}

uint32_t vgpu_primary_res(void) {
    return g_scan_res;
}

int vgpu_res_live_count(void) {
    return gpu_res_live(&g_pool);
}

int vgpu_ctx_live_count(void) {
    return gpu_ctx_live(&g_pool);
}

void vgpu_fb_size(uint32_t *w, uint32_t *h) {
    if (w) {
        *w = g_fb_w;
    }
    if (h) {
        *h = g_fb_h;
    }
}

uint32_t vgpu_capset_n(void) {
    return g_cap_n;
}

int vgpu_capset_at(uint32_t index, uint32_t *id, uint32_t *ver, uint32_t *size,
                   const uint8_t **data) {
    if (index >= g_cap_n) {
        return -1;
    }
    if (id) {
        *id = g_cap[index].id;
    }
    if (ver) {
        *ver = g_cap[index].ver;
    }
    if (size) {
        *size = g_cap[index].size;
    }
    if (data) {
        *data = g_cap[index].data;
    }
    return 0;
}

uint64_t vgpu_dma_phys(int dma) {
    return ((uint64_t)hw_dma_hi(dma) << 32) | (uint64_t)hw_dma_lo(dma);
}

int vgpu_alloc_dma(int pages) {
    return hw_dma_alloc(pages);
}

void vgpu_free_dma(int dma) {
    if (dma >= 0) {
        (void)hw_dma_free(dma);
    }
}

uint8_t *vgpu_dma_ptr(int dma) {
    return hw_dma_ptr(dma);
}

const uint8_t *vgpu_resp(void) {
    return hw_dma_ptr(g_resp_dma);
}

uint32_t vgpu_resp_cap(void) {
    return hw_dma_bytes(g_resp_dma);
}

static void vgpu_log_err(const char *what, uint32_t cmd, uint32_t res,
                         uint32_t ctx, uint64_t fence, uint32_t resp,
                         const char *why) {
    serial_puts("vgpu error op=");
    serial_puts(what);
    serial_puts(" cmd=");
    serial_write_hex(cmd);
    serial_puts(" res=");
    serial_write_u64(res);
    serial_puts(" ctx=");
    serial_write_u64(ctx);
    serial_puts(" fence=");
    serial_write_u64(fence);
    serial_puts(" resp=");
    serial_write_hex(resp);
    serial_puts(" ");
    serial_puts(why);
    serial_puts("\n");
}

void vgpu_on_irq(void) {
    if (g_isr_ok) {
        (void)hw_mmio_r8(g_isr_win, g_isr_off);
    }
    g_irq_count++;
}

static void vgpu_irq(struct irq_frame *frame) {
    vgpu_on_irq();
    if (g_irq_prev && g_irq_prev != vgpu_irq) {
        g_irq_prev(frame);
    }
}

static uint64_t vgpu_cycles(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int vgpu_kick(VqBind *vq, uint32_t cmd_len, uint32_t resp_max, uint64_t fence,
                     int slow, int check_resp, uint32_t *resp_type, uint32_t *used_len) {
    uint8_t *cmd;
    uint8_t *resp;
    uint16_t head;
    uint16_t second;
    uint32_t spins;
    uint32_t limit;
    uint64_t cphys;
    uint64_t rphys;
    if (!vq || !vq->ready || g_dead || cmd_len < VGPU_HDR_SIZE) {
        return -1;
    }
    cmd = hw_dma_ptr(g_cmd_dma);
    resp = hw_dma_ptr(g_resp_dma);
    if (!cmd || !resp || cmd_len > hw_dma_bytes(g_cmd_dma) ||
        resp_max > hw_dma_bytes(g_resp_dma) || resp_max < VGPU_HDR_SIZE) {
        return -1;
    }
    vgpu_wr32(cmd, 4, fence ? VGPU_FLAG_FENCE : 0u);
    vgpu_wr32(cmd, 8, (uint32_t)fence);
    vgpu_wr32(cmd, 12, (uint32_t)(fence >> 32));
    {
        uint32_t i;
        for (i = 0; i < 32u; ++i) {
            resp[i] = 0;
        }
    }
    if (virtq_alloc(&vq->q, 2, &head) != 0) {
        vgpu_log_err("virtqueue", vgpu_rd32(cmd, 0), 0, vgpu_rd32(cmd, 16), fence,
                     0, "descriptor leak or full");
        g_dead = 1;
        return -1;
    }
    second = virtq_next(&vq->q, head);
    cphys = vgpu_dma_phys(g_cmd_dma);
    rphys = vgpu_dma_phys(g_resp_dma);
    if (virtq_set(&vq->q, head, cphys, cmd_len, 0) != 0 ||
        virtq_set(&vq->q, second, rphys, resp_max, VQ_DESC_F_WRITE) != 0 ||
        virtq_publish(&vq->q, head) != 0) {
        virtq_reclaim(&vq->q, head);
        return -1;
    }
    if (hw_mmio_w16(vq->notify_win, vq->notify_off, (uint16_t)vq->index) != 0) {
        g_dead = 1;
        g_live = 0;
        return -1;
    }
    limit = slow ? VGPU_SPIN_SLOW : VGPU_SPIN_FAST;
    {
        uint64_t t0 = vgpu_cycles();
        uint64_t budget = slow ? VGPU_TSC_SLOW : VGPU_TSC_FAST;
        for (spins = 0; spins < limit; ++spins) {
            uint16_t id = 0;
            uint32_t len = 0;
            int took;
            if ((spins & 63u) == 0u) {
                __asm__ volatile ("pause");
                if (spins > 1000u && vgpu_cycles() - t0 > budget) {
                    break;
                }
            }
            took = virtq_take(&vq->q, &id, &len);
            if (took < 0) {
                g_dead = 1;
                g_live = 0;
                vgpu_log_err("virtqueue", vgpu_rd32(cmd, 0), 0, vgpu_rd32(cmd, 16),
                             fence, 0, "used id");
                return -1;
            }
            if (took == 1) {
                uint32_t typ;
                uint64_t got;
                g_waits++;
                if (id != head) {
                    virtq_reclaim(&vq->q, id);
                } else {
                    virtq_reclaim(&vq->q, head);
                }
                if (g_isr_ok) {
                    (void)hw_mmio_r8(g_isr_win, g_isr_off);
                }
                typ = vgpu_rd32(resp, 0);
                got = (uint64_t)vgpu_rd32(resp, 8) |
                      ((uint64_t)vgpu_rd32(resp, 12) << 32);
                if (resp_type) {
                    *resp_type = typ;
                }
                if (used_len) {
                    *used_len = len;
                }
                if (!check_resp) {
                    return 0;
                }
                if (fence != 0u && got != fence) {
                    vgpu_log_err("fence", vgpu_rd32(cmd, 0), 0, vgpu_rd32(cmd, 16),
                                 fence, typ, "fence mismatch");
                    return -3;
                }
                if (typ >= VGPU_RESP_ERR_UNSPEC || typ == 0u) {
                    vgpu_log_err("response", vgpu_rd32(cmd, 0), 0, vgpu_rd32(cmd, 16),
                                 fence, typ, typ == 0u ? "empty response" : "device error");
                    return -3;
                }
                return 0;
            }
        }
    }
    g_dead = 1;
    g_live = 0;
    g_online = 0;
    vgpu_log_err("timeout", vgpu_rd32(cmd, 0), 0, vgpu_rd32(cmd, 16), fence, 0,
                 "timeout");
    return -2;
}

static int vgpu_submit_vq(VqBind *vq, const uint8_t *src, uint32_t len,
                          uint32_t expect, int slow) {
    uint8_t *cmd;
    uint32_t i;
    uint32_t typ = 0;
    uint64_t fence;
    uint32_t cmd_type;
    if (!g_online || g_busy || !src) {
        return -1;
    }
    cmd = hw_dma_ptr(g_cmd_dma);
    if (!cmd || len > hw_dma_bytes(g_cmd_dma)) {
        return -1;
    }
    g_busy = 1;
    for (i = 0; i < len; ++i) {
        cmd[i] = src[i];
    }
    fence = g_fence_seq++;
    cmd_type = vgpu_rd32(cmd, 0);
    if (vgpu_kick(vq, len, hw_dma_bytes(g_resp_dma), fence, slow, 1, &typ, 0) != 0) {
        g_busy = 0;
        return -1;
    }
    g_busy = 0;
    if (expect != 0u && typ != expect) {
        vgpu_log_err("response", cmd_type, 0, vgpu_rd32(cmd, 16), fence, typ,
                     "unexpected response");
        return -3;
    }
    return 0;
}

int vgpu_submit(const uint8_t *cmd, uint32_t cmd_len, uint32_t expect_resp,
                uint64_t fence, uint32_t *resp_type, uint32_t *used_len) {
    (void)fence;
    (void)resp_type;
    (void)used_len;
    return vgpu_submit_vq(&g_ctrl, cmd, cmd_len, expect_resp, 0);
}

int vgpu_submit3d(uint32_t ctx, const uint32_t *dwords, uint32_t ndwords) {
    uint8_t *cmd = hw_dma_ptr(g_cmd_dma);
    uint32_t len = 0;
    uint32_t typ = 0;
    uint64_t fence;
    if (!cmd || !dwords || ctx == 0u || g_busy || !g_online) {
        return -1;
    }
    if (vgpu_enc_submit3d(cmd, hw_dma_bytes(g_cmd_dma), ctx, dwords, ndwords,
                          &len) != 0) {
        vgpu_log_err("SUBMIT_3D", VGPU_CMD_SUBMIT_3D, 0, ctx, 0, 0, "encode");
        return -1;
    }
    g_busy = 1;
    fence = g_fence_seq++;
    if (vgpu_debug()) {
        serial_puts("[VIRGL] submit ctx=");
        serial_write_u64(ctx);
        serial_puts(" dwords=");
        serial_write_u64(ndwords);
        serial_puts(" fence=");
        serial_write_u64(fence);
        serial_puts("\n");
    }
    if (vgpu_kick(&g_ctrl, len, 256u, fence, 1, 1, &typ, 0) != 0) {
        g_busy = 0;
        return -1;
    }
    g_busy = 0;
    if (typ != VGPU_RESP_OK_NODATA) {
        vgpu_log_err("SUBMIT_3D", VGPU_CMD_SUBMIT_3D, 0, ctx, fence, typ,
                     "unexpected response");
        return -3;
    }
    if (vgpu_debug()) {
        serial_puts("[VIRGL] complete fence=");
        serial_write_u64(fence);
        serial_puts("\n");
    }
    return 0;
}

static int cmd_ok_wait(uint32_t type, const uint8_t *buf, uint32_t len, int slow) {
    return vgpu_submit_vq(&g_ctrl, buf, len,
                          type == VGPU_CMD_GET_CAPSET           ? VGPU_RESP_OK_CAPSET
                          : type == VGPU_CMD_GET_CAPSET_INFO    ? VGPU_RESP_OK_CAPSET_INFO
                          : type == VGPU_CMD_GET_DISPLAY_INFO   ? VGPU_RESP_OK_DISPLAY_INFO
                                                                : VGPU_RESP_OK_NODATA,
                          slow);
}

static int cmd_ok(uint32_t type, uint32_t ctx, const uint8_t *buf, uint32_t len) {
    (void)ctx;
    return cmd_ok_wait(type, buf, len, 0);
}

int vgpu_res_attach(int owner, uint32_t id) {
    GpuResource *r = gpu_res_get(&g_pool, id);
    uint8_t buf[48];
    uint32_t len = 0;
    uint64_t phys;
    if (!r || r->owner != owner || r->dma < 0 || r->backing_size == 0u) {
        return -1;
    }
    if (r->backing_off > hw_dma_bytes(r->dma) ||
        r->backing_size > hw_dma_bytes(r->dma) - r->backing_off) {
        return -1;
    }
    phys = vgpu_dma_phys(r->dma) + (uint64_t)r->backing_off;
    if (vgpu_enc_attach(buf, sizeof buf, id, phys, r->backing_size, &len) != 0) {
        return -1;
    }
    if (cmd_ok(VGPU_CMD_RESOURCE_ATTACH_BACKING, 0, buf, len) != 0) {
        vgpu_log_err("ATTACH_BACKING", VGPU_CMD_RESOURCE_ATTACH_BACKING, id, 0, 0,
                     0, "attach failed");
        return -1;
    }
    r->state = GPU_ST_BACKED;
    return 0;
}

int vgpu_res_detach(int owner, uint32_t id) {
    GpuResource *r = gpu_res_get(&g_pool, id);
    uint8_t buf[32];
    uint32_t len = 0;
    if (!r || r->owner != owner) {
        return -1;
    }
    if (vgpu_enc_simple(buf, sizeof buf, VGPU_CMD_RESOURCE_DETACH_BACKING, 0, id,
                        &len) != 0) {
        return -1;
    }
    if (cmd_ok(VGPU_CMD_RESOURCE_DETACH_BACKING, 0, buf, len) != 0) {
        return -1;
    }
    if (r->state == GPU_ST_BACKED) {
        r->state = GPU_ST_CREATED;
    }
    return 0;
}

int vgpu_res_unref(int owner, uint32_t id) {
    GpuResource *r = gpu_res_get(&g_pool, id);
    uint8_t buf[32];
    uint32_t len = 0;
    int dma;
    if (!r || r->owner != owner) {
        return -1;
    }
    dma = r->dma;
    if (vgpu_enc_simple(buf, sizeof buf, VGPU_CMD_RESOURCE_UNREF, 0, id, &len) !=
        0) {
        return -1;
    }
    if (cmd_ok(VGPU_CMD_RESOURCE_UNREF, 0, buf, len) != 0) {
        vgpu_log_err("UNREF", VGPU_CMD_RESOURCE_UNREF, id, 0, 0, 0, "unref failed");
        return -1;
    }
    if (gpu_res_release(&g_pool, owner, id) != 0) {
        return -1;
    }
    (void)dma;
    return 0;
}

int vgpu_res_drop(int owner, uint32_t id) {
    GpuResource *r = gpu_res_get(&g_pool, id);
    int dma;
    if (!r || r->owner != owner) {
        return -1;
    }
    dma = r->dma;
    if (vgpu_res_unref(owner, id) != 0) {
        return -1;
    }
    if (dma >= 0 && dma != g_fb_dma && dma != g_cmd_dma && dma != g_resp_dma &&
        dma != g_ctrl.dma && dma != g_cursor.dma && dma != g_cur_dma) {
        vgpu_free_dma(dma);
    }
    return 0;
}

int vgpu_res_create_2d(int owner, uint32_t fmt, uint32_t w, uint32_t h, int dma,
                       uint32_t backing, uint32_t *id) {
    uint32_t rid = 0;
    uint8_t buf[48];
    uint32_t len = 0;
    GpuResource *r;
    if (!id || gpu_res_alloc(&g_pool, owner, &rid) != 0) {
        return -1;
    }
    if (vgpu_enc_create_2d(buf, sizeof buf, rid, fmt, w, h, &len) != 0 ||
        cmd_ok(VGPU_CMD_RESOURCE_CREATE_2D, 0, buf, len) != 0) {
        (void)gpu_res_release(&g_pool, owner, rid);
        vgpu_log_err("CREATE_2D", VGPU_CMD_RESOURCE_CREATE_2D, rid, 0, 0, 0,
                     "create failed");
        return -1;
    }
    r = gpu_res_get(&g_pool, rid);
    r->type = GPU_RES_2D;
    r->state = GPU_ST_CREATED;
    r->width = w;
    r->height = h;
    r->depth = 1;
    r->format = fmt;
    r->dma = dma;
    r->backing_size = backing;
    if (dma >= 0) {
        if (vgpu_res_attach(owner, rid) != 0) {
            (void)vgpu_res_unref(owner, rid);
            return -1;
        }
    }
    *id = rid;
    return 0;
}

int vgpu_res_create_3d_off(int owner, const VgpuCreate3D *info, int dma, uint32_t off,
                           uint32_t backing, uint32_t *id) {
    int rc = vgpu_res_create_3d(owner, info, -1, 0, id);
    GpuResource *r;
    if (rc != 0) {
        return rc;
    }
    r = gpu_res_get(&g_pool, *id);
    if (!r) {
        return -1;
    }
    r->dma = dma;
    r->backing_off = off;
    r->backing_size = backing;
    if (dma >= 0 && backing > 0u) {
        if (vgpu_res_attach(owner, *id) != 0) {
            (void)vgpu_res_unref(owner, *id);
            return -1;
        }
    }
    return 0;
}

int vgpu_res_create_3d(int owner, const VgpuCreate3D *info, int dma,
                       uint32_t backing, uint32_t *id) {
    VgpuCreate3D local;
    uint32_t rid = 0;
    uint8_t buf[80];
    uint32_t len = 0;
    GpuResource *r;
    if (!info || !id || gpu_res_alloc(&g_pool, owner, &rid) != 0) {
        return -1;
    }
    local = *info;
    local.resource_id = rid;
    if (vgpu_enc_create_3d(buf, sizeof buf, &local, &len) != 0 ||
        cmd_ok_wait(VGPU_CMD_RESOURCE_CREATE_3D, buf, len, 1) != 0) {
        (void)gpu_res_release(&g_pool, owner, rid);
        vgpu_log_err("CREATE_3D", VGPU_CMD_RESOURCE_CREATE_3D, rid, 0, 0, 0,
                     "create failed");
        return -1;
    }
    r = gpu_res_get(&g_pool, rid);
    r->type = GPU_RES_3D;
    r->state = GPU_ST_CREATED;
    r->width = local.width;
    r->height = local.height;
    r->depth = local.depth;
    r->format = local.format;
    r->bind = local.bind;
    r->flags = local.flags;
    r->dma = dma;
    r->backing_size = backing;
    if (vgpu_debug()) {
        serial_puts("[VIRGL] resource create id=");
        serial_write_u64(rid);
        serial_puts(" target=");
        serial_write_u64(local.target);
        serial_puts("\n");
    }
    if (dma >= 0 && backing > 0u) {
        if (vgpu_res_attach(owner, rid) != 0) {
            (void)vgpu_res_unref(owner, rid);
            return -1;
        }
    }
    *id = rid;
    return 0;
}

int vgpu_res_flush(uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint8_t buf[48];
    uint32_t len = 0;
    if (vgpu_enc_flush(buf, sizeof buf, id, x, y, w, h, &len) != 0) {
        return -1;
    }
    return cmd_ok(VGPU_CMD_RESOURCE_FLUSH, 0, buf, len);
}

int vgpu_res_xfer2d(uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint64_t off) {
    uint8_t buf[56];
    uint32_t len = 0;
    if (vgpu_enc_xfer2d(buf, sizeof buf, id, x, y, w, h, off, &len) != 0) {
        return -1;
    }
    return cmd_ok(VGPU_CMD_TRANSFER_TO_HOST_2D, 0, buf, len);
}

int vgpu_set_scanout(uint32_t scanout, uint32_t id, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h) {
    uint8_t buf[48];
    uint32_t len = 0;
    GpuResource *r = gpu_res_get(&g_pool, id);
    if (vgpu_enc_scanout(buf, sizeof buf, id, scanout, x, y, w, h, &len) != 0) {
        return -1;
    }
    if (cmd_ok(VGPU_CMD_SET_SCANOUT, 0, buf, len) != 0) {
        vgpu_log_err("SET_SCANOUT", VGPU_CMD_SET_SCANOUT, id, 0, 0, 0, "scanout");
        return -1;
    }
    if (r) {
        r->state = GPU_ST_SCANOUT;
    }
    /* QEMU drops the cursor sprite when the scanout resource changes.
     * MOVE_CURSOR does not define it again. */
    g_cur_gen++;
    return 0;
}

int vgpu_xfer3d(int to_host, uint32_t ctx, const VgpuXfer3D *box) {
    uint8_t buf[80];
    uint32_t len = 0;
    uint32_t type = to_host ? VGPU_CMD_TRANSFER_TO_HOST_3D
                            : VGPU_CMD_TRANSFER_FROM_HOST_3D;
    if (vgpu_enc_xfer3d(buf, sizeof buf, type, ctx, box, &len) != 0) {
        return -1;
    }
    return cmd_ok_wait(type, buf, len, 1);
}

int vgpu_ctx_create(int owner, uint32_t capset, int context_init, uint32_t *ctx) {
    uint32_t id = 0;
    uint8_t buf[96];
    uint32_t len = 0;
    uint32_t init = 0;
    if (!ctx || gpu_ctx_alloc(&g_pool, owner, capset, &id) != 0) {
        return -1;
    }
    if (context_init && (g_neg_lo & (1u << VGPU_F_CONTEXT_INIT)) != 0u) {
        init = capset & 0xffu;
    }
    if (vgpu_enc_ctx_create(buf, sizeof buf, id, 7u, init, "chrisos", &len) != 0 ||
        cmd_ok_wait(VGPU_CMD_CTX_CREATE, buf, len, 1) != 0) {
        (void)gpu_ctx_release(&g_pool, owner, id);
        vgpu_log_err("CTX_CREATE", VGPU_CMD_CTX_CREATE, 0, id, 0, 0, "ctx create");
        return -1;
    }
    if (vgpu_debug()) {
        serial_puts("[VIRGL] ctx create id=");
        serial_write_u64(id);
        serial_puts("\n");
    }
    *ctx = id;
    return 0;
}

int vgpu_ctx_destroy(int owner, uint32_t ctx) {
    uint8_t buf[32];
    uint32_t len = 0;
    GpuContext *c = gpu_ctx_get(&g_pool, ctx);
    if (!c || c->owner != owner) {
        return -1;
    }
    vgpu_hdr(buf, VGPU_CMD_CTX_DESTROY, 0, 0, ctx);
    len = VGPU_HDR_SIZE;
    if (cmd_ok_wait(VGPU_CMD_CTX_DESTROY, buf, len, 1) != 0) {
        vgpu_log_err("CTX_DESTROY", VGPU_CMD_CTX_DESTROY, 0, ctx, 0, 0, "ctx destroy");
        return -1;
    }
    return gpu_ctx_release(&g_pool, owner, ctx);
}

int vgpu_ctx_attach(uint32_t ctx, uint32_t res) {
    uint8_t buf[32];
    uint32_t len = 0;
    GpuContext *c = gpu_ctx_get(&g_pool, ctx);
    GpuResource *r = gpu_res_get(&g_pool, res);
    if (!c || !c->live || !r || c->owner != r->owner) {
        return -1;
    }
    if (vgpu_enc_simple(buf, sizeof buf, VGPU_CMD_CTX_ATTACH_RESOURCE, ctx, res,
                        &len) != 0 ||
        cmd_ok_wait(VGPU_CMD_CTX_ATTACH_RESOURCE, buf, len, 1) != 0) {
        vgpu_log_err("CTX_ATTACH", VGPU_CMD_CTX_ATTACH_RESOURCE, res, ctx, 0, 0,
                     "attach resource");
        return -1;
    }
    r->ctx_attached++;
    if (vgpu_debug()) {
        serial_puts("[VIRGL] attach resource=");
        serial_write_u64(res);
        serial_puts(" ctx=");
        serial_write_u64(ctx);
        serial_puts("\n");
    }
    return 0;
}

int vgpu_ctx_detach(uint32_t ctx, uint32_t res) {
    uint8_t buf[32];
    uint32_t len = 0;
    GpuContext *c = gpu_ctx_get(&g_pool, ctx);
    GpuResource *r = gpu_res_get(&g_pool, res);
    if (!c || !c->live || !r || c->owner != r->owner) {
        return -1;
    }
    if (vgpu_enc_simple(buf, sizeof buf, VGPU_CMD_CTX_DETACH_RESOURCE, ctx, res,
                        &len) != 0 ||
        cmd_ok_wait(VGPU_CMD_CTX_DETACH_RESOURCE, buf, len, 1) != 0) {
        return -1;
    }
    if (r && r->ctx_attached > 0) {
        r->ctx_attached--;
    }
    return 0;
}

static int vgpu_stress(int cycles) {
    int dma;
    int i;
    int before = gpu_res_live(&g_pool);
    uint16_t dfree;
    dma = hw_dma_alloc(1);
    if (dma < 0) {
        serial_puts("FAIL: vgpu resource stress dma\n");
        return -1;
    }
    dfree = virtq_nfree(&g_ctrl.q);
    for (i = 0; i < cycles; ++i) {
        uint32_t id = 0;
        if (vgpu_res_create_2d(VGPU_OWNER_KERNEL, VGPU_FORMAT_B8G8R8A8, 8, 8, dma,
                               256u, &id) != 0 ||
            vgpu_res_xfer2d(id, 0, 0, 8, 8, 0) != 0 ||
            vgpu_res_flush(id, 0, 0, 8, 8) != 0 ||
            vgpu_res_detach(VGPU_OWNER_KERNEL, id) != 0 ||
            vgpu_res_unref(VGPU_OWNER_KERNEL, id) != 0) {
            serial_puts("FAIL: vgpu resource stress cycle ");
            serial_write_u64((uint64_t)i);
            serial_puts("\n");
            vgpu_free_dma(dma);
            return -1;
        }
    }
    vgpu_free_dma(dma);
    if (gpu_res_live(&g_pool) != before || virtq_nfree(&g_ctrl.q) != dfree) {
        serial_puts("FAIL: vgpu resource stress leak res=");
        serial_write_u64((uint64_t)gpu_res_live(&g_pool));
        serial_puts(" desc=");
        serial_write_u64(virtq_nfree(&g_ctrl.q));
        serial_puts("\n");
        return -1;
    }
    serial_puts("PASS: vgpu resource stress ");
    serial_write_u64((uint64_t)cycles);
    serial_puts("\n");
    return 0;
}

void vgpu_flush_rect(int x, int y, int w, int h) {
    int x1;
    int y1;
    int row;
    int col;
    uint8_t *fb;
    if (!vgpu_ready() || !g_gfx.front || w < 1 || h < 1) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= (int)g_fb_w || y >= (int)g_fb_h) {
        return;
    }
    x1 = x + w;
    y1 = y + h;
    if (x1 > (int)g_fb_w) {
        x1 = (int)g_fb_w;
    }
    if (y1 > (int)g_fb_h) {
        y1 = (int)g_fb_h;
    }
    if (x1 > g_gfx.width) {
        x1 = g_gfx.width;
    }
    if (y1 > g_gfx.height) {
        y1 = g_gfx.height;
    }
    if (x1 <= x || y1 <= y) {
        return;
    }
    fb = hw_dma_ptr(g_fb_dma);
    if (!fb) {
        return;
    }
    for (row = y; row < y1; ++row) {
        uint32_t *dst = (uint32_t *)(fb + ((uint32_t)row * g_fb_w + (uint32_t)x) * 4u);
        const uint32_t *src = g_gfx.front + row * g_gfx.pitch_pixels + x;
        for (col = 0; col < x1 - x; ++col) {
            dst[col] = src[col] | 0xFF000000u;
        }
    }
    if (vgpu_res_xfer2d(g_scan_res, (uint32_t)x, (uint32_t)y, (uint32_t)(x1 - x),
                        (uint32_t)(y1 - y),
                        (uint64_t)(y * (int)g_fb_w + x) * 4u) != 0) {
        return;
    }
    (void)vgpu_res_flush(g_scan_res, (uint32_t)x, (uint32_t)y, (uint32_t)(x1 - x),
                         (uint32_t)(y1 - y));
    g_rects++;
    g_pixels += (uint32_t)((x1 - x) * (y1 - y));
    g_bytes += (uint32_t)((x1 - x) * (y1 - y) * 4);
    if (x == 0 && y == 0 && x1 == (int)g_fb_w && y1 == (int)g_fb_h) {
        g_full++;
    } else {
        g_partial++;
    }
}

/* QEMU allocates a 64x64 cursor and ignores any other resource size. */
#define VGPU_CUR_DIM 64u

static int cursor_kick(uint32_t type, uint32_t x, uint32_t y) {
    uint8_t buf[56];
    uint32_t len = 0;
    uint8_t *cmd;
    uint32_t i;
    uint32_t typ = 0;
    uint64_t fence;
    if (!g_cursor.ready || g_cur_res == 0u || g_dead || !hw_dma_ptr(g_cursor.dma)) {
        return -1;
    }
    if (vgpu_enc_cursor(buf, sizeof buf, type, g_cur_res, x, y, 0, 0, &len) != 0) {
        return -1;
    }
    cmd = hw_dma_ptr(g_cmd_dma);
    if (!cmd || g_busy) {
        return -2;
    }
    g_busy = 1;
    for (i = 0; i < len; ++i) {
        cmd[i] = buf[i];
    }
    fence = g_fence_seq++;
    if (vgpu_kick(&g_cursor, len, 64u, fence, 0, 0, &typ, 0) != 0) {
        g_busy = 0;
        g_cursor_on = 0;
        return -1;
    }
    g_busy = 0;
    if (type == VGPU_CMD_UPDATE_CURSOR) {
        g_cur_sent = g_cur_gen;
    }
    return 0;
}

int vgpu_cursor_move(int x, int y) {
    uint32_t type;
    int rc;
    if (!g_cursor_on || x < 0 || y < 0) {
        return -1;
    }
    g_cur_x = x;
    g_cur_y = y;
    /* SET_SCANOUT clears the host sprite. The next command must be
     * UPDATE_CURSOR; MOVE_CURSOR only stores a position. */
    type = g_cur_sent != g_cur_gen ? VGPU_CMD_UPDATE_CURSOR : VGPU_CMD_MOVE_CURSOR;
    rc = cursor_kick(type, (uint32_t)x, (uint32_t)y);
    if (rc == 0) {
        return 0;
    }
    if (rc == -1 && !g_cursor_on) {
        serial_puts("vgpu cursor fallback software\n");
    }
    return -1;
}

static int vgpu_cursor_setup(void) {
    uint32_t id = 0;
    uint8_t *px;
    int y;
    int x;
    if (!g_cursor.ready) {
        return 0;
    }
    g_cur_dma = hw_dma_alloc(4);
    if (g_cur_dma < 0) {
        return 0;
    }
    px = hw_dma_ptr(g_cur_dma);
    for (y = 0; y < (int)VGPU_CUR_DIM; ++y) {
        for (x = 0; x < (int)VGPU_CUR_DIM; ++x) {
            uint32_t c = 0;
            if (x < 12 && y < 12 && (x < 2 || y < 2 || x == y)) {
                c = 0xFFFFFFFFu;
            }
            ((uint32_t *)px)[y * (int)VGPU_CUR_DIM + x] = c;
        }
    }
    if (vgpu_res_create_2d(VGPU_OWNER_KERNEL, VGPU_FORMAT_B8G8R8A8, VGPU_CUR_DIM,
                           VGPU_CUR_DIM, g_cur_dma,
                           VGPU_CUR_DIM * VGPU_CUR_DIM * 4u, &id) != 0) {
        vgpu_free_dma(g_cur_dma);
        g_cur_dma = -1;
        return 0;
    }
    g_cur_res = id;
    g_cursor_on = 1;
    if (cursor_kick(VGPU_CMD_UPDATE_CURSOR, 0, 0) != 0) {
        g_cursor_on = 0;
        serial_puts("vgpu cursor software fallback\n");
        return 0;
    }
    serial_puts("virtio-gpu cursorq\n");
    return 1;
}

static int find_gpu(int *bus_out, int *dev_out) {
    int bus;
    int dev;
    for (bus = 0; bus < 8; ++bus) {
        for (dev = 0; dev < 32; ++dev) {
            uint32_t id = pci_read((uint8_t)bus, (uint8_t)dev, 0, 0);
            if ((id & 0xFFFFu) == 0xFFFFu) {
                continue;
            }
            if ((id & 0xFFFFu) == 0x1AF4u && (id >> 16) == 0x1050u) {
                *bus_out = bus;
                *dev_out = dev;
                return 1;
            }
        }
    }
    return 0;
}

static int setup_queue(int cwin, uint32_t coff, int nwin, uint32_t noff,
                       uint32_t mult, int qindex, VqBind *vq) {
    uint16_t maxsz;
    uint16_t use;
    uint32_t bytes;
    uint64_t phys;
    uint16_t qnote;
    vq->ready = 0;
    vq->index = qindex;
    vq->dma = -1;
    hw_mmio_w16(cwin, coff + 22, (uint16_t)qindex);
    maxsz = (uint16_t)hw_mmio_r16(cwin, coff + 24);
    use = VGPU_QSZ;
    if (maxsz < use) {
        use = maxsz;
    }
    if (use >= 16u) {
        use = 16u;
    } else if (use >= 8u) {
        use = 8u;
    } else if (use >= 4u) {
        use = 4u;
    } else if (use >= 2u) {
        use = 2u;
    } else {
        return -1;
    }
    bytes = virtq_bytes(use);
    if (bytes == 0u || bytes > 4096u) {
        return -1;
    }
    vq->dma = hw_dma_alloc(1);
    if (vq->dma < 0 || virtq_init(&vq->q, hw_dma_ptr(vq->dma), 4096u, use) != 0) {
        return -1;
    }
    phys = vgpu_dma_phys(vq->dma);
    hw_mmio_w16(cwin, coff + 24, use);
    hw_mmio_w16(cwin, coff + 26, 0xffffu);
    hw_mmio_w32(cwin, coff + 32, (uint32_t)phys);
    hw_mmio_w32(cwin, coff + 36, (uint32_t)(phys >> 32));
    hw_mmio_w32(cwin, coff + 40, (uint32_t)(phys + virtq_avail_off(&vq->q)));
    hw_mmio_w32(cwin, coff + 44, (uint32_t)(phys >> 32));
    hw_mmio_w32(cwin, coff + 48, (uint32_t)(phys + virtq_used_off(&vq->q)));
    hw_mmio_w32(cwin, coff + 52, (uint32_t)(phys >> 32));
    hw_mmio_w16(cwin, coff + 28, 1u);
    qnote = (uint16_t)hw_mmio_r16(cwin, coff + 30);
    vq->notify_win = nwin;
    vq->notify_off = noff + (uint32_t)qnote * mult;
    vq->ready = 1;
    return 0;
}

static int negotiate(int cwin, uint32_t coff, int want_virgl) {
    uint32_t lo;
    uint32_t hi;
    hw_mmio_w8(cwin, coff + 20, 0);
    hw_mmio_w8(cwin, coff + 20, 1);
    hw_mmio_w8(cwin, coff + 20, 3);
    hw_mmio_w32(cwin, coff, 0);
    lo = hw_mmio_r32(cwin, coff + 4);
    hw_mmio_w32(cwin, coff, 1);
    hi = hw_mmio_r32(cwin, coff + 4);
    g_dev_lo = lo;
    g_dev_hi = hi;
    g_req_lo = 0;
    g_req_hi = 0;
    if ((hi & 1u) == 0u) {
        return -1;
    }
    g_req_hi = 1u;
    if (want_virgl && (lo & (1u << VGPU_F_VIRGL)) != 0u) {
        g_req_lo |= 1u << VGPU_F_VIRGL;
    }
    if ((g_req_lo & (1u << VGPU_F_VIRGL)) != 0u &&
        (lo & (1u << VGPU_F_CONTEXT_INIT)) != 0u) {
        g_req_lo |= 1u << VGPU_F_CONTEXT_INIT;
    }
    hw_mmio_w32(cwin, coff + 8, 0);
    hw_mmio_w32(cwin, coff + 12, g_req_lo);
    hw_mmio_w32(cwin, coff + 8, 1);
    hw_mmio_w32(cwin, coff + 12, g_req_hi);
    hw_mmio_w8(cwin, coff + 20, 11);
    if ((hw_mmio_r8(cwin, coff + 20) & 8u) == 0u) {
        g_req_lo = 0;
        hw_mmio_w8(cwin, coff + 20, 0);
        hw_mmio_w8(cwin, coff + 20, 1);
        hw_mmio_w8(cwin, coff + 20, 3);
        hw_mmio_w32(cwin, coff + 8, 0);
        hw_mmio_w32(cwin, coff + 12, 0);
        hw_mmio_w32(cwin, coff + 8, 1);
        hw_mmio_w32(cwin, coff + 12, 1u);
        hw_mmio_w8(cwin, coff + 20, 11);
        if ((hw_mmio_r8(cwin, coff + 20) & 8u) == 0u) {
            return -1;
        }
    }
    g_neg_lo = g_req_lo;
    g_neg_hi = g_req_hi;
    return 0;
}

static void read_display(int dwin, uint32_t doff) {
    uint8_t buf[32];
    uint32_t len = 0;
    const uint8_t *resp;
    g_num_scanouts = 1;
    g_num_capsets = 0;
    if (dwin >= 0) {
        g_num_scanouts = hw_mmio_r32(dwin, doff + 8);
        g_num_capsets = hw_mmio_r32(dwin, doff + 12);
        if (g_num_capsets > 16u) {
            g_num_capsets = 0;
        }
    }
    vgpu_hdr(buf, VGPU_CMD_GET_DISPLAY_INFO, 0, 0, 0);
    len = VGPU_HDR_SIZE;
    if (cmd_ok(VGPU_CMD_GET_DISPLAY_INFO, 0, buf, len) != 0) {
        serial_puts("virtio-gpu display info failed\n");
        return;
    }
    resp = vgpu_resp();
    if (!resp) {
        return;
    }
    serial_puts("virtio-gpu scanout0 ");
    serial_write_u64(vgpu_rd32(resp, 32));
    serial_puts("x");
    serial_write_u64(vgpu_rd32(resp, 36));
    serial_puts(" enabled=");
    serial_write_u64(vgpu_rd32(resp, 40));
    serial_puts(" scanouts=");
    serial_write_u64(g_num_scanouts);
    serial_puts("\n");
}

static int read_capsets(void) {
    uint32_t i;
    g_cap_n = 0;
    if ((g_neg_lo & (1u << VGPU_F_VIRGL)) == 0u) {
        return 0;
    }
    if (g_num_capsets == 0u) {
        serial_puts("virgl capsets missing\n");
        return -1;
    }
    for (i = 0; i < g_num_capsets && g_cap_n < VGPU_CAP_MAX; ++i) {
        uint8_t buf[32];
        uint32_t len = 0;
        uint32_t id;
        uint32_t ver;
        uint32_t sz;
        const uint8_t *resp;
        uint32_t n;
        uint32_t copy;
        if (vgpu_enc_capset_info(buf, sizeof buf, i, &len) != 0 ||
            cmd_ok(VGPU_CMD_GET_CAPSET_INFO, 0, buf, len) != 0) {
            vgpu_log_err("GET_CAPSET_INFO", VGPU_CMD_GET_CAPSET_INFO, 0, 0, 0, 0,
                         "capset info");
            return -1;
        }
        resp = vgpu_resp();
        id = vgpu_rd32(resp, 24);
        ver = vgpu_rd32(resp, 28);
        sz = vgpu_rd32(resp, 32);
        if (id != VGPU_CAPSET_VIRGL && id != VGPU_CAPSET_VIRGL2) {
            continue;
        }
        if (sz > VGPU_CAP_BYTES || ver == 0u) {
            serial_puts("virgl capset rejected size=");
            serial_write_u64(sz);
            serial_puts("\n");
            continue;
        }
        if (vgpu_enc_capset(buf, sizeof buf, id, ver > 2u ? 2u : ver, &len) != 0 ||
            cmd_ok(VGPU_CMD_GET_CAPSET, 0, buf, len) != 0) {
            vgpu_log_err("GET_CAPSET", VGPU_CMD_GET_CAPSET, id, 0, 0, 0, "capset");
            return -1;
        }
        resp = vgpu_resp();
        copy = sz;
        if (copy > VGPU_CAP_BYTES) {
            copy = VGPU_CAP_BYTES;
        }
        g_cap[g_cap_n].id = id;
        g_cap[g_cap_n].ver = ver;
        g_cap[g_cap_n].size = copy;
        for (n = 0; n < copy; ++n) {
            g_cap[g_cap_n].data[n] = resp[VGPU_HDR_SIZE + n];
        }
        g_cap_n++;
    }
    serial_puts("number of capsets: ");
    serial_write_u64(g_cap_n);
    serial_puts("\n");
    return g_cap_n > 0u ? 0 : -1;
}

int vgpu_boot(int width, int height) {
    int bus;
    int dev;
    int off;
    int cwin = -1;
    int nwin = -1;
    int dwin = -1;
    uint32_t coff = 0;
    uint32_t noff = 0;
    uint32_t doff = 0;
    uint32_t mult = 0;
    uint32_t cmdreg;
    int pages;
    int want3d;
    if (g_live) {
        return 1;
    }
    gpu_pool_init(&g_pool);
    if (bootflag_gfx_fb() || width < 1 || height < 1) {
        serial_puts("gfx backend framebuffer\n");
        serial_puts("3D backend -> software\n");
        (void)gfx3d_boot(GFX3D_SOFTWARE);
        return 0;
    }
    if (!find_gpu(&bus, &dev)) {
        serial_puts("virtio-gpu miss\n");
        serial_puts("3D backend -> software\n");
        (void)gfx3d_boot(GFX3D_SOFTWARE);
        return 0;
    }
    serial_puts("VirtIO GPU detected\n");
    cmdreg = pci_read((uint8_t)bus, (uint8_t)dev, 0, 4);
    hw_pci_write(bus, dev, 0, 4, cmdreg | 6u);
    g_irq_line = (uint8_t)(pci_read((uint8_t)bus, (uint8_t)dev, 0, 0x3c) & 0xffu);
    off = (int)(pci_read((uint8_t)bus, (uint8_t)dev, 0, 0x34) & 0xffu);
    g_isr_ok = 0;
    while (off > 0) {
        uint32_t cap = pci_read((uint8_t)bus, (uint8_t)dev, 0, (uint8_t)off);
        int typ = (int)((cap >> 24) & 0xffu);
        int next = (int)((cap >> 8) & 0xffu);
        if ((cap & 0xffu) == 9u) {
            int bar = (int)(pci_read((uint8_t)bus, (uint8_t)dev, 0,
                                     (uint8_t)(off + 4)) &
                            0xffu);
            uint32_t cfg = pci_read((uint8_t)bus, (uint8_t)dev, 0,
                                    (uint8_t)(off + 8));
            int win = hw_bar_map(bus, dev, 0, bar);
            if (typ == 1) {
                cwin = win;
                coff = cfg;
            }
            if (typ == 2) {
                nwin = win;
                noff = cfg;
                mult = pci_read((uint8_t)bus, (uint8_t)dev, 0,
                                (uint8_t)(off + 16));
            }
            if (typ == 3 && win >= 0) {
                g_isr_win = win;
                g_isr_off = cfg;
                g_isr_ok = 1;
            }
            if (typ == 4 && win >= 0) {
                dwin = win;
                doff = cfg;
            }
        }
        off = next;
    }
    if (cwin < 0 || nwin < 0) {
        serial_puts("virtio-gpu no cap\n");
        return 0;
    }
    want3d = bootflag_gfx3d() != 1;
    if (negotiate(cwin, coff, want3d) != 0) {
        serial_puts("virtio-gpu features\n");
        return 0;
    }
    g_cwin = cwin;
    g_coff = coff;
    serial_puts("VIRGL feature: ");
    serial_puts((g_neg_lo & (1u << VGPU_F_VIRGL)) ? "yes\n" : "no\n");
    serial_puts("CONTEXT_INIT: ");
    serial_puts((g_neg_lo & (1u << VGPU_F_CONTEXT_INIT)) ? "yes\n" : "no\n");
    if (width > 1920) {
        width = 1920;
    }
    if (height > 1080) {
        height = 1080;
    }
    g_fb_w = (uint32_t)width;
    g_fb_h = (uint32_t)height;
    pages = (int)((g_fb_w * g_fb_h * 4u + 4095u) / 4096u);
    g_cmd_dma = hw_dma_alloc(4);
    g_resp_dma = hw_dma_alloc(4);
    g_fb_dma = hw_dma_alloc(pages);
    if (g_cmd_dma < 0 || g_resp_dma < 0 || g_fb_dma < 0) {
        serial_puts("virtio-gpu dma\n");
        return 0;
    }
    if (setup_queue(cwin, coff, nwin, noff, mult, 0, &g_ctrl) != 0) {
        serial_puts("virtio-gpu queue\n");
        return 0;
    }
    if (hw_mmio_r16(cwin, coff + 18) >= 2u) {
        if (setup_queue(cwin, coff, nwin, noff, mult, 1, &g_cursor) != 0) {
            g_cursor.ready = 0;
        }
    }
    hw_mmio_w8(cwin, coff + 20, 15);
    g_online = 1;
    if (g_irq_line < 16u) {
        g_irq_prev = irq_get_handler(g_irq_line);
        if (g_irq_prev == vgpu_irq) {
            g_irq_prev = 0;
        }
        irq_set_handler(g_irq_line, vgpu_irq);
        pic_set_mask(g_irq_line, false);
    }
    read_display(dwin, doff);
    if (vgpu_res_create_2d(VGPU_OWNER_KERNEL, VGPU_FORMAT_B8G8R8A8, g_fb_w, g_fb_h,
                           g_fb_dma, g_fb_w * g_fb_h * 4u, &g_scan_res) != 0 ||
        vgpu_set_scanout(0, g_scan_res, 0, 0, g_fb_w, g_fb_h) != 0) {
        serial_puts("virtio-gpu scanout\n");
        g_online = 0;
        return 0;
    }
    g_live = 1;
    serial_puts("virtio-gpu ready ");
    serial_write_u64(g_fb_w);
    serial_puts("x");
    serial_write_u64(g_fb_h);
    serial_puts("\n");
    (void)vgpu_cursor_setup();
    if (vgpu_stress(bootflag_gfx_stress() ? 1000 : 4) != 0) {
        serial_puts("3D backend -> software\n");
        (void)gfx3d_boot(GFX3D_SOFTWARE);
        return 1;
    }
    if ((g_neg_lo & (1u << VGPU_F_VIRGL)) == 0u || bootflag_gfx3d() == 1) {
        if (bootflag_gfx3d() == 2) {
            serial_puts("VirGL failure\n");
            serial_puts("reason feature not negotiated\n");
        }
        serial_puts("3D backend -> software\n");
        (void)gfx3d_boot(bootflag_gfx3d() == 2 ? GFX3D_VIRGL : GFX3D_SOFTWARE);
        return 1;
    }
    if (read_capsets() != 0) {
        serial_puts("VirGL failure\n");
        serial_puts("reason capset\n");
        serial_puts("3D backend -> software\n");
        (void)gfx3d_boot(GFX3D_SOFTWARE);
        return 1;
    }
    if (gfx3d_boot(bootflag_gfx3d() == 2 ? GFX3D_VIRGL : GFX3D_AUTO) != 0) {
        serial_puts("VirGL failure\n");
        serial_puts("reason gfx3d boot\n");
        serial_puts("3D backend -> software\n");
        return 1;
    }
    if (virgl_demo_run() != 0) {
        gfx3d_mark_lost();
        serial_puts("VirGL failure\n");
        serial_puts("3D backend -> software\n");
        return 1;
    }
    serial_puts("3D backend -> virgl\n");
    return 1;
}
