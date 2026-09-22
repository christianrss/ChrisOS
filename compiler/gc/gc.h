#ifndef CHRIS_GC_H
#define CHRIS_GC_H

#include <stdint.h>

#define GC_TYPE_MAX 256

void gc_init(void);
void *gc_alloc(uint32_t type_id, uint32_t size);
void gc_root_reg(void **slot);
void gc_request(void);
int gc_pending(void);
void gc_poll(void);
void gc_collect(void);
uint32_t gc_live(void);
void gc_type_clear(void);
int gc_type_register(uint32_t type_id, uint32_t size, uint32_t gc_bits,
                     const char *name);
int gc_type_lookup(uint32_t type_id, uint32_t *size_out, uint32_t *gc_bits_out);

#endif
