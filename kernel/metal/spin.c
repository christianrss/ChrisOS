#include "spin.h"

int cas_u32(volatile uint32_t *cell, uint32_t expected, uint32_t desired) {
    return __sync_bool_compare_and_swap(cell, expected, desired);
}

void spin_init(Spinlock *lock) {
    lock->locked = 0u;
}

void spin_lock(Spinlock *lock) {
    for (;;) {
        if (cas_u32(&lock->locked, 0u, 1u)) {
            return;
        }
        __asm__ volatile ("pause");
    }
}

void spin_unlock(Spinlock *lock) {
    __sync_lock_release(&lock->locked);
}

uint32_t atomic_add_u32(volatile uint32_t *cell, uint32_t delta) {
    return (uint32_t)__sync_fetch_and_add(cell, delta);
}
