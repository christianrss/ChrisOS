#ifndef CHRISOS_PMM_H
#define CHRISOS_PMM_H

#include <stdint.h>

#define PMM_PAGE     4096ull
#define PMM_MAX_PHYS (256ull * 1024ull * 1024ull)

void pmm_init(void);
void pmm_selftest(void);
uint64_t pmm_alloc(void);
void pmm_free(uint64_t phys);
uint64_t pmm_usable_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_free_pages(void);

#endif
