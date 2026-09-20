#ifndef CHRISOS_HEAP_H
#define CHRISOS_HEAP_H

#include <stdint.h>

void heap_init(void);
void heap_selftest(void);
void *kmalloc(uint64_t size);
void kfree(void *ptr);
uint64_t heap_used_bytes(void);
uint64_t heap_free_bytes(void);
uint64_t heap_arena_count(void);

#endif
