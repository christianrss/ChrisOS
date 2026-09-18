#ifndef CHRISOS_BOOTINFO_H
#define CHRISOS_BOOTINFO_H

#include <stdint.h>

struct bootinfo {
    uint64_t hhdm_offset;
    uint64_t fb_addr;
    uint64_t fb_width;
    uint64_t fb_height;
    uint64_t fb_pitch;
    uint16_t fb_bpp;
    uint64_t usable_bytes;
    uint64_t memmap_entries;
    uint64_t cpu_count;
    uint32_t bsp_lapic_id;
};

void bootinfo_init(void);
const struct bootinfo *bootinfo_get(void);
uint64_t bootinfo_phys_to_virt(uint64_t phys);
uint64_t bootinfo_memmap_count(void);
int bootinfo_memmap_entry(uint64_t index, uint64_t *base, uint64_t *length, uint64_t *type);

#endif
