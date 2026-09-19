#ifndef CHRIS_SPIN_H
#define CHRIS_SPIN_H

#include <stdint.h>

typedef struct {
    volatile uint32_t locked;
} Spinlock;

void spin_init(Spinlock *lock);
void spin_lock(Spinlock *lock);
void spin_unlock(Spinlock *lock);
uint32_t atomic_add_u32(volatile uint32_t *cell, uint32_t delta);
int cas_u32(volatile uint32_t *cell, uint32_t expected, uint32_t desired);

#endif
