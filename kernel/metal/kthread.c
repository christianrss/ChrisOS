#include "kthread.h"

#include "job.h"
#include "heap.h"
#include <stdint.h>

#define KT_STACK 32768u

typedef struct KT {
    int used;
    int done;
    KThreadFn fn;
    void *arg;
    void *tls[8];
    void *stack;
    void *saved_rsp;
} KT;

static KT g_th[KTHREAD_MAX];
static volatile int g_cur = -1;

static KT *g_run;
static void *g_saved_rsp;

static void kt_trampoline(void) __attribute__((used));

static void kt_trampoline(void) {
    if (g_run && g_run->fn)
        g_run->fn(g_run->arg);
}

static void kt_run(KT *t) {
    void *top;
    if (!t->fn)
        return;
    if (!t->stack) {
        t->fn(t->arg);
        return;
    }
    g_run = t;
    top = (void *)(((uintptr_t)t->stack + KT_STACK) & ~(uintptr_t)15);
    __asm__ volatile (
        "movq %%rsp, %[saved]\n\t"
        "movq %[top], %%rsp\n\t"
        "call kt_trampoline\n\t"
        "movq %[saved], %%rsp\n\t"
        : [saved] "=m" (g_saved_rsp)
        : [top] "r" (top)
        : "memory", "cc", "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11"
    );
}

static void kt_job(void *arg, uint32_t cpu) {
    KT *t = (KT *)arg;
    int prev = g_cur;
    (void)cpu;
    g_cur = (int)(t - g_th);
    kt_run(t);
    t->done = 1;
    g_cur = prev;
}

int kthread_create(KThreadFn fn, void *arg) {
    int i;
    for (i = 0; i < KTHREAD_MAX; ++i) {
        if (!g_th[i].used) {
            g_th[i].used = 1;
            g_th[i].done = 0;
            g_th[i].fn = fn;
            g_th[i].arg = arg;
            g_th[i].stack = kmalloc(KT_STACK);
            if (job_submit(kt_job, &g_th[i]) == 0) {
                int prev = g_cur;
                g_cur = i;
                kt_run(&g_th[i]);
                g_th[i].done = 1;
                g_cur = prev;
            }
            return i;
        }
    }
    return -1;
}

void kthread_join(int id) {
    if (id < 0 || id >= KTHREAD_MAX)
        return;
    while (!g_th[id].done)
        job_worker_once(0);
    if (g_th[id].stack) {
        kfree(g_th[id].stack);
        g_th[id].stack = 0;
    }
    g_th[id].used = 0;
}

int kthread_self(void) {
    return g_cur;
}

void *kthread_tls(int index) {
    int id = kthread_self();
    if (id < 0 || id >= KTHREAD_MAX || index < 0 || index >= 8)
        return 0;
    return g_th[id].tls[index];
}

void kthread_tls_set(int index, void *v) {
    int id = kthread_self();
    if (id < 0 || id >= KTHREAD_MAX || index < 0 || index >= 8)
        return;
    g_th[id].tls[index] = v;
}

void kmutex_init(KMutex *m) {
    spin_init(&m->lock);
}

void kmutex_lock(KMutex *m) {
    spin_lock(&m->lock);
}

void kmutex_unlock(KMutex *m) {
    spin_unlock(&m->lock);
}

void kcond_init(KCond *c) {
    c->seq = 0;
}

void kcond_wait(KCond *c, KMutex *m) {
    uint32_t s = c->seq;
    kmutex_unlock(m);
    while (c->seq == s)
        job_worker_once(0);
    kmutex_lock(m);
}

void kcond_signal(KCond *c) {
    c->seq++;
}
