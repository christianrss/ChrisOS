#ifndef CHRISOS_MEMINFO_H
#define CHRISOS_MEMINFO_H

#include <stdint.h>

int mem_format(char *dst, uint32_t cap, uint64_t pmm_used_pages,
               uint64_t pmm_free_pages, uint64_t heap_used,
               uint64_t heap_free, int tasks);

#endif
