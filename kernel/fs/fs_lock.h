#ifndef CHRIS_FS_LOCK_H
#define CHRIS_FS_LOCK_H

#include <stdint.h>

/* Yielding lock for filesystem metadata. Waiters pause with interrupts left
 * enabled, so a holder may poll a disk without wedging the IRQ path.
 * Do not take this lock from an interrupt handler.
 * Same holder may re-enter (cfs_read calls cfs_read_at). */

typedef struct FsLock {
    volatile uint32_t locked;
    volatile uint32_t owner;
    uint32_t depth;
} FsLock;

extern FsLock g_cfs_lock;

/* Host builds keep a weak holder of 1 so one thread re-enters and a stress
 * test can override the id. The kernel build returns smp_current_cpu()+1, so
 * two CPUs are different owners. 0 is rewritten to 1. */
uint32_t fs_lock_holder(void);

static inline int fs_cas_u32(volatile uint32_t *cell, uint32_t expected,
                             uint32_t desired) {
    return __sync_bool_compare_and_swap(cell, expected, desired);
}

static inline void fs_lock_enter(FsLock *lock) {
    uint32_t me;
    if (!lock) {
        return;
    }
    me = fs_lock_holder();
    if (me == 0u) {
        me = 1u;
    }
    if (lock->locked && lock->owner == me) {
        lock->depth++;
        return;
    }
    for (;;) {
        if (fs_cas_u32(&lock->locked, 0u, 1u)) {
            lock->owner = me;
            lock->depth = 1u;
            return;
        }
        if (lock->owner == me && lock->locked) {
            lock->depth++;
            return;
        }
        __asm__ volatile ("pause");
    }
}

static inline void fs_lock_leave(FsLock *lock) {
    if (!lock || !lock->locked) {
        return;
    }
    if (lock->depth > 1u) {
        lock->depth--;
        return;
    }
    lock->depth = 0u;
    lock->owner = 0u;
    __sync_lock_release(&lock->locked);
}

static inline void fs_lock_cleanup(FsLock **held) {
    if (held && *held) {
        fs_lock_leave(*held);
    }
}

#define CFS_LOCK() \
    FsLock *_fs_guard __attribute__((cleanup(fs_lock_cleanup), unused)) = \
        (fs_lock_enter(&g_cfs_lock), &g_cfs_lock)

#endif
