#ifndef CHRISOS_MM_H
#define CHRISOS_MM_H

#include <stdint.h>

#define MM_PRESENT (1ull << 0)
#define MM_WRITE   (1ull << 1)
#define MM_USER    (1ull << 2)
#define MM_PWT     (1ull << 3)
#define MM_PCD     (1ull << 4)
#define MM_NX      (1ull << 63)

void mm_init(void);
void mm_selftest(void);
void map_4k(uint64_t virt, uint64_t phys, uint64_t flags);
void map_4k_nosync(uint64_t virt, uint64_t phys, uint64_t flags);
void mm_flush_tlb(void);
void *map_mmio_page(uint64_t phys);
void *mm_lapic_virt(void);
uint64_t mm_virt_to_phys(uint64_t virt);

#endif
