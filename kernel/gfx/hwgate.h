#ifndef CHRIS_HWGATE_H
#define CHRIS_HWGATE_H

#include <stdint.h>

int hw_pci_write(int bus, int dev, int fn, int off, uint32_t val);
int hw_bar_map(int bus, int dev, int fn, int bar);
uint32_t hw_mmio_r32(int win, uint32_t off);
int hw_mmio_w32(int win, uint32_t off, uint32_t val);
uint32_t hw_mmio_r8(int win, uint32_t off);
int hw_mmio_w8(int win, uint32_t off, uint32_t val);
uint32_t hw_mmio_r16(int win, uint32_t off);
int hw_mmio_w16(int win, uint32_t off, uint32_t val);
int hw_dma_alloc(int pages);
uint32_t hw_dma_lo(int id);
uint32_t hw_dma_hi(int id);
int hw_dma_w32(int id, uint32_t off, uint32_t val);
uint32_t hw_dma_r32(int id, uint32_t off);
uint32_t hw_disk_sectors(void);
int hw_disk_read(uint32_t lba, uint8_t *dst, int nsec);
int hw_disk_write(uint32_t lba, const uint8_t *src, int nsec);
int hw_disk_format(void);
int hw_gpu_arm(int q_dma, int cmd_dma, int fb_dma, int w, int h,
               int notify_win, uint32_t notify_off);
void hw_gpu_flush(void);

#endif
