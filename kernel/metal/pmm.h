#ifndef CHRISOS_PMM_H
#define CHRISOS_PMM_H

#include <stdint.h>

#define PMM_PAGE     4096ull
#define PMM_MAX_PHYS (256ull * 1024ull * 1024ull)

void pmm_init(void);
void pmm_selftest(void);
uint64_t pmm_alloc(void);
uint64_t pmm_alloc_contig(uint32_t pages);
void pmm_free(uint64_t phys);
void pmm_free_contig(uint64_t phys, uint32_t pages);
uint64_t pmm_usable_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_free_pages(void);

#endif
