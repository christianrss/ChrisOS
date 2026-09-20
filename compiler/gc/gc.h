#ifndef CHRIS_GC_H
#define CHRIS_GC_H

#include <stdint.h>

void gc_init(void);
void *gc_alloc(uint32_t type_id, uint32_t size);
void gc_root_reg(void **slot);
void gc_request(void);
int gc_pending(void);
void gc_poll(void);
void gc_collect(void);
uint32_t gc_live(void);

#endif
