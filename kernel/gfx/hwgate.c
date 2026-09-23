#include "hwgate.h"

#include "block_device.h"
#include "bootinfo.h"
#include "graphics.h"
#include "mm.h"
#include "pci.h"
#include "pmm.h"
#include "storage.h"

#define HW_WIN 4
#define HW_WIN_PAGES 16
#define HW_DMA 4
#define HW_DMA_PAGES 64
#define HW_DISK_MAX 8

typedef struct HwWin {
    int used;
    uint64_t phys;
    volatile uint8_t *virt;
} HwWin;

typedef struct HwDma {
    int used;
    int pages;
    uint64_t phys;
    uint8_t *virt;
} HwDma;

typedef struct HwGpu {
    int live;
    uint8_t *q;
    uint64_t q_phys;
    uint8_t *cmd;
    uint64_t cmd_phys;
    uint8_t *fb;
    int fb_bytes;
    int w;
    int h;
    int notify_win;
    uint32_t notify_off;
    uint16_t avail;
} HwGpu;

static HwWin g_win[HW_WIN];
static HwDma g_dma[HW_DMA];
static HwGpu g_gpu;
static uint8_t g_disk_buf[HW_DISK_MAX * 512];

static uint8_t *ram_virt(uint64_t phys) {
    return (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
}

int hw_pci_write(int bus, int dev, int fn, int off, uint32_t val) {
    if (bus < 0 || dev < 0 || fn < 0 || off < 0) {
        return -1;
    }
    pci_write((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, (uint8_t)off, val);
    return 0;
}

int hw_bar_map(int bus, int dev, int fn, int bar) {
    uint32_t raw;
    uint32_t hi;
    uint64_t phys;
    int i;
    int page;
    volatile uint8_t *base;

    if (bus < 0 || dev < 0 || fn < 0 || bar < 0 || bar > 5) {
        return -1;
    }
    raw = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                   (uint8_t)(0x10 + bar * 4));
    if (raw & 1u) {
        return -1;
    }
    phys = (uint64_t)(raw & ~0xFu);
    if (((raw >> 1) & 3u) == 2u && bar < 5) {
        hi = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                      (uint8_t)(0x10 + (bar + 1) * 4));
        phys |= (uint64_t)hi << 32;
    }
    phys &= ~4095ull;
    if (phys == 0) {
        return -1;
    }
    for (i = 0; i < HW_WIN; ++i) {
        if (g_win[i].used && g_win[i].phys == phys) {
            return i;
        }
    }
    for (i = 0; i < HW_WIN; ++i) {
        if (!g_win[i].used) {
            break;
        }
    }
    if (i == HW_WIN) {
        return -1;
    }
    base = 0;
    for (page = 0; page < HW_WIN_PAGES; ++page) {
        void *v = map_mmio_page(phys + (uint64_t)page * 4096ull);
        if (page == 0) {
            base = (volatile uint8_t *)v;
        }
    }
    g_win[i].used = 1;
    g_win[i].phys = phys;
    g_win[i].virt = base;
    return i;
}

static volatile uint8_t *win_ptr(int win, uint32_t off) {
    if (win < 0 || win >= HW_WIN || !g_win[win].used) {
        return 0;
    }
    if (off >= (uint32_t)HW_WIN_PAGES * 4096u) {
        return 0;
    }
    return g_win[win].virt + off;
}

uint32_t hw_mmio_r32(int win, uint32_t off) {
    volatile uint8_t *p = win_ptr(win, off);
    uint32_t v;
    if (!p || (off & 3u)) {
        return 0;
    }
    v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
    return v;
}

int hw_mmio_w32(int win, uint32_t off, uint32_t val) {
    volatile uint8_t *p = win_ptr(win, off);
    if (!p || (off & 3u)) {
        return -1;
    }
    p[0] = (uint8_t)val;
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
    return 0;
}

uint32_t hw_mmio_r8(int win, uint32_t off) {
    volatile uint8_t *p = win_ptr(win, off);
    if (!p) {
        return 0;
    }
    return p[0];
}

int hw_mmio_w8(int win, uint32_t off, uint32_t val) {
    volatile uint8_t *p = win_ptr(win, off);
    if (!p) {
        return -1;
    }
    p[0] = (uint8_t)val;
    return 0;
}

uint32_t hw_mmio_r16(int win, uint32_t off) {
    volatile uint8_t *p = win_ptr(win, off);
    if (!p || (off & 1u)) {
        return 0;
    }
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

int hw_mmio_w16(int win, uint32_t off, uint32_t val) {
    volatile uint8_t *p = win_ptr(win, off);
    if (!p || (off & 1u)) {
        return -1;
    }
    p[0] = (uint8_t)val;
    p[1] = (uint8_t)(val >> 8);
    return 0;
}

int hw_dma_alloc(int pages) {
    int i;
    uint64_t phys;
    uint8_t *virt;
    int n;

    if (pages < 1 || pages > HW_DMA_PAGES) {
        return -1;
    }
    for (i = 0; i < HW_DMA; ++i) {
        if (!g_dma[i].used) {
            break;
        }
    }
    if (i == HW_DMA) {
        return -1;
    }
    phys = pmm_alloc_contig((uint64_t)pages);
    if (phys == 0) {
        return -1;
    }
    virt = ram_virt(phys);
    for (n = 0; n < pages * 4096; ++n) {
        virt[n] = 0;
    }
    g_dma[i].used = 1;
    g_dma[i].pages = pages;
    g_dma[i].phys = phys;
    g_dma[i].virt = virt;
    return i;
}

uint32_t hw_dma_lo(int id) {
    if (id < 0 || id >= HW_DMA || !g_dma[id].used) {
        return 0;
    }
    return (uint32_t)g_dma[id].phys;
}

uint32_t hw_dma_hi(int id) {
    if (id < 0 || id >= HW_DMA || !g_dma[id].used) {
        return 0;
    }
    return (uint32_t)(g_dma[id].phys >> 32);
}

static uint8_t *dma_ptr(int id, uint32_t off, uint32_t n) {
    if (id < 0 || id >= HW_DMA || !g_dma[id].used) {
        return 0;
    }
    if (off + n > (uint32_t)g_dma[id].pages * 4096u) {
        return 0;
    }
    return g_dma[id].virt + off;
}

int hw_dma_w32(int id, uint32_t off, uint32_t val) {
    uint8_t *p = dma_ptr(id, off, 4);
    if (!p || (off & 3u)) {
        return -1;
    }
    p[0] = (uint8_t)val;
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
    return 0;
}

uint32_t hw_dma_r32(int id, uint32_t off) {
    uint8_t *p = dma_ptr(id, off, 4);
    if (!p || (off & 3u)) {
        return 0;
    }
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint32_t hw_disk_sectors(void) {
    BlockDevice *d = storage_disk();
    if (!d) {
        return 0;
    }
    return d->sector_count;
}

int hw_disk_read(uint32_t lba, uint8_t *dst, int nsec) {
    BlockDevice *d = storage_disk();
    if (!d || !dst || nsec < 1 || nsec > HW_DISK_MAX) {
        return -1;
    }
    if (bd_read(d, lba, (uint32_t)nsec, g_disk_buf) != BD_OK) {
        return -1;
    }
    {
        int i;
        int n = nsec * 512;
        for (i = 0; i < n; ++i) {
            dst[i] = g_disk_buf[i];
        }
    }
    return 0;
}

int hw_disk_write(uint32_t lba, const uint8_t *src, int nsec) {
    BlockDevice *d = storage_disk();
    int i;
    int n;
    if (!d || !src || nsec < 1 || nsec > HW_DISK_MAX) {
        return -1;
    }
    if (lba < 64u) {
        return -1;
    }
    n = nsec * 512;
    for (i = 0; i < n; ++i) {
        g_disk_buf[i] = src[i];
    }
    if (bd_write(d, lba, (uint32_t)nsec, g_disk_buf) != BD_OK) {
        return -1;
    }
    return 0;
}

int hw_disk_format(void) {
    return storage_reformat();
}

int hw_gpu_arm(int q_dma, int cmd_dma, int fb_dma, int w, int h,
               int notify_win, uint32_t notify_off) {
    if (q_dma < 0 || cmd_dma < 0 || fb_dma < 0 || w < 1 || h < 1) {
        return -1;
    }
    if (q_dma >= HW_DMA || cmd_dma >= HW_DMA || fb_dma >= HW_DMA) {
        return -1;
    }
    if (!g_dma[q_dma].used || !g_dma[cmd_dma].used || !g_dma[fb_dma].used) {
        return -1;
    }
    if (w * h * 4 > g_dma[fb_dma].pages * 4096) {
        return -1;
    }
    g_gpu.q = g_dma[q_dma].virt;
    g_gpu.q_phys = g_dma[q_dma].phys;
    g_gpu.cmd = g_dma[cmd_dma].virt;
    g_gpu.cmd_phys = g_dma[cmd_dma].phys;
    g_gpu.fb = g_dma[fb_dma].virt;
    g_gpu.fb_bytes = w * h * 4;
    g_gpu.w = w;
    g_gpu.h = h;
    g_gpu.notify_win = notify_win;
    g_gpu.notify_off = notify_off;
    g_gpu.avail = (uint16_t)g_dma[q_dma].virt[66] |
                  (uint16_t)((uint16_t)g_dma[q_dma].virt[67] << 8);
    g_gpu.live = 1;
    return 0;
}

static void put32(uint8_t *p, uint32_t off, uint32_t v) {
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
    p[off + 2] = (uint8_t)(v >> 16);
    p[off + 3] = (uint8_t)(v >> 24);
}

static int gpu_kick(uint32_t len) {
    uint16_t idx;
    int spins;
    uint32_t used;

    put32(g_gpu.q, 0, (uint32_t)g_gpu.cmd_phys);
    put32(g_gpu.q, 4, (uint32_t)(g_gpu.cmd_phys >> 32));
    put32(g_gpu.q, 8, len);
    put32(g_gpu.q, 12, 1u | (1u << 16));
    put32(g_gpu.q, 16, (uint32_t)(g_gpu.cmd_phys + 256u));
    put32(g_gpu.q, 20, (uint32_t)((g_gpu.cmd_phys + 256u) >> 32));
    put32(g_gpu.q, 24, 64u);
    put32(g_gpu.q, 28, 2u);
    g_gpu.avail = (uint16_t)(g_gpu.avail + 1u);
    idx = g_gpu.avail;
    g_gpu.q[64] = 0;
    g_gpu.q[65] = 0;
    g_gpu.q[66] = (uint8_t)idx;
    g_gpu.q[67] = (uint8_t)(idx >> 8);
    if (hw_mmio_w16(g_gpu.notify_win, g_gpu.notify_off, 0) != 0) {
        g_gpu.live = 0;
        return -1;
    }
    for (spins = 0; spins < 100000; ++spins) {
        used = (uint32_t)g_gpu.q[2048] | ((uint32_t)g_gpu.q[2049] << 8) |
               ((uint32_t)g_gpu.q[2050] << 16) | ((uint32_t)g_gpu.q[2051] << 24);
        if ((used >> 16) == idx) {
            return 0;
        }
    }
    g_gpu.live = 0;
    return -1;
}

static void gpu_rect(uint32_t type, uint32_t len) {
    int i;
    for (i = 0; i < 64; ++i) {
        g_gpu.cmd[i] = 0;
        g_gpu.cmd[256 + i] = 0;
    }
    put32(g_gpu.cmd, 0, type);
    put32(g_gpu.cmd, 32, (uint32_t)g_gpu.w);
    put32(g_gpu.cmd, 36, (uint32_t)g_gpu.h);
    if (type == 0x0105u) {
        put32(g_gpu.cmd, 48, 1u);
    } else {
        put32(g_gpu.cmd, 40, 1u);
    }
    (void)len;
    (void)gpu_kick(type == 0x0105u ? 56u : 48u);
}

void hw_gpu_flush(void) {
    int x;
    int y;
    int gw;
    int gh;
    if (!g_gpu.live || !g_gfx.front) {
        return;
    }
    gw = g_gfx.width < g_gpu.w ? g_gfx.width : g_gpu.w;
    gh = g_gfx.height < g_gpu.h ? g_gfx.height : g_gpu.h;
    for (y = 0; y < gh; ++y) {
        for (x = 0; x < gw; ++x) {
            uint32_t p = g_gfx.front[y * g_gfx.pitch_pixels + x];
            p |= 0xFF000000u;
            put32(g_gpu.fb, (uint32_t)((y * g_gpu.w + x) * 4), p);
        }
    }
    gpu_rect(0x0105u, 56u);
    if (!g_gpu.live) {
        return;
    }
    gpu_rect(0x0104u, 48u);
}
