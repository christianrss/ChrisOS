#ifndef CHRISOS_PMM_H
#define CHRISOS_PMM_H

#include <stdint.h>

#define PMM_PAGE     4096ull
#define PMM_MAX_PHYS (32ull * 1024ull * 1024ull * 1024ull)
#define PMM_KEEP     (32ull * 1024ull * 1024ull)

void pmm_init(void);
void pmm_selftest(void);
uint64_t pmm_alloc(void);
uint64_t pmm_alloc_contig(uint64_t pages);
/* Pages reserved below 4GB for UHCI. VirtIO can use high RAM; UHCI cannot. */
uint64_t pmm_alloc_dma32(uint64_t pages);
void pmm_free_dma32(uint64_t phys, uint64_t pages);
int pmm_dma32_owns(uint64_t phys);
void pmm_free(uint64_t phys);
void pmm_free_contig(uint64_t phys, uint64_t pages);
void pmm_foreach_free_run(int (*cb)(uint64_t phys, uint64_t pages, void *user),
                          void *user);
uint64_t pmm_claim_at(uint64_t phys, uint64_t pages);
uint64_t pmm_usable_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_free_pages(void);

#endif
