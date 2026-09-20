#ifndef CHRIS_KTHREAD_H
#define CHRIS_KTHREAD_H

#include <stdint.h>
#include "spin.h"

#define KTHREAD_MAX 32

/* Cooperative jobs with a private stack each. The callback must return;
 * there is no preemption. kthread_self() is the slot id. */

typedef void (*KThreadFn)(void *arg);

typedef struct KMutex {
    Spinlock lock;
} KMutex;

typedef struct KCond {
    volatile uint32_t seq;
} KCond;

int kthread_create(KThreadFn fn, void *arg);
void kthread_join(int id);
int kthread_self(void);
void *kthread_tls(int index);
void kthread_tls_set(int index, void *v);
void kmutex_init(KMutex *m);
void kmutex_lock(KMutex *m);
void kmutex_unlock(KMutex *m);
void kcond_init(KCond *c);
void kcond_wait(KCond *c, KMutex *m);
void kcond_signal(KCond *c);

#endif
