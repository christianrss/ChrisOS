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

/* Save interrupt flag and disable interrupts. Host tests define
 * CHRIS_HOST_METAL so CLI/STI (privileged) stay a no-op. */
static inline uint64_t irq_save(void) {
#ifdef CHRIS_HOST_METAL
    return 0;
#else
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
#endif
}

static inline void irq_restore(uint64_t flags) {
#ifndef CHRIS_HOST_METAL
    if ((flags & 0x200ull) != 0) {
        __asm__ volatile ("sti");
    }
#else
    (void)flags;
#endif
}

#endif
