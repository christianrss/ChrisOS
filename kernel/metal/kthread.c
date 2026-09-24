#include "kthread.h"

#include "heap.h"
#include "job.h"
#include "smp.h"
#include <stdint.h>

#define KT_STACK 32768u

typedef struct KT {
    int used;
    volatile int done;
    KThreadFn fn;
    void *arg;
    void *tls[8];
    void *stack;
    void *saved_rsp;
} KT;

static KT g_th[KTHREAD_MAX];
static Spinlock g_slot_lock;
/* Per CPU. A single g_cur / g_saved_rsp is unsafe: two CPUs in kt_run
 * would restore each other's stack. */
static int g_cur_cpu[SMP_CPU_CAP] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};
static KT *g_run_cpu[SMP_CPU_CAP];

static void kt_trampoline(void) __attribute__((used));

static uint32_t kt_cpu(void) {
    uint32_t cpu = smp_current_cpu();
    if (cpu >= SMP_CPU_CAP) {
        return 0u;
    }
    return cpu;
}

static void kt_trampoline(void) {
    KT *t = g_run_cpu[kt_cpu()];
    if (t && t->fn) {
        t->fn(t->arg);
    }
}

static void kt_run(KT *t) {
    void *top;
    uint32_t cpu;
    KT *prev_run;

    if (!t->fn || !t->stack) {
        return;
    }
    cpu = kt_cpu();
    prev_run = g_run_cpu[cpu];
    g_run_cpu[cpu] = t;
    top = (void *)(((uintptr_t)t->stack + KT_STACK) & ~(uintptr_t)15);
    __asm__ volatile (
        "movq %%rsp, %[saved]\n\t"
        "movq %[top], %%rsp\n\t"
        "call kt_trampoline\n\t"
        "movq %[saved], %%rsp\n\t"
        : [saved] "=m" (t->saved_rsp)
        : [top] "r" (top)
        : "memory", "cc", "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11"
    );
    g_run_cpu[cpu] = prev_run;
}

static void kt_job(void *arg, uint32_t cpu_index) {
    KT *t = (KT *)arg;
    uint32_t cpu = kt_cpu();
    int prev = g_cur_cpu[cpu];
    (void)cpu_index;
    g_cur_cpu[cpu] = (int)(t - g_th);
    kt_run(t);
    t->done = 1;
    g_cur_cpu[cpu] = prev;
}

int kthread_create(KThreadFn fn, void *arg) {
    int i;
    void *stack;

    if (!fn) {
        return -1;
    }
    spin_lock(&g_slot_lock);
    for (i = 0; i < KTHREAD_MAX; ++i) {
        if (!g_th[i].used) {
            g_th[i].used = 1;
            g_th[i].done = 0;
            g_th[i].fn = fn;
            g_th[i].arg = arg;
            g_th[i].stack = 0;
            break;
        }
    }
    spin_unlock(&g_slot_lock);
    if (i == KTHREAD_MAX) {
        return -1;
    }
    stack = kmalloc(KT_STACK);
    if (!stack) {
        spin_lock(&g_slot_lock);
        g_th[i].used = 0;
        g_th[i].fn = 0;
        spin_unlock(&g_slot_lock);
        return -1;
    }
    g_th[i].stack = stack;
    if (job_submit(kt_job, &g_th[i]) == 0) {
        /* Queue full: run here. Still on this CPU's context, not a
         * borrowed global RSP. */
        uint32_t cpu = kt_cpu();
        int prev = g_cur_cpu[cpu];
        g_cur_cpu[cpu] = i;
        kt_run(&g_th[i]);
        g_th[i].done = 1;
        g_cur_cpu[cpu] = prev;
    }
    return i;
}

void kthread_join(int id) {
    void *stack;

    if (id < 0 || id >= KTHREAD_MAX) {
        return;
    }
    /* Uniprocessor must drain the queue or the job never runs.
     * With APs online, do not pull arbitrary jobs onto the joiner:
     * that re-enters unrelated work under the waiter. */
    while (!g_th[id].done) {
        if (cpu_online_count < 2u) {
            job_worker_once(kt_cpu());
        } else {
            __asm__ volatile ("pause");
        }
    }
    spin_lock(&g_slot_lock);
    stack = g_th[id].stack;
    g_th[id].stack = 0;
    g_th[id].used = 0;
    g_th[id].fn = 0;
    g_th[id].done = 0;
    spin_unlock(&g_slot_lock);
    if (stack) {
        kfree(stack);
    }
}

int kthread_self(void) {
    return g_cur_cpu[kt_cpu()];
}

void *kthread_tls(int index) {
    int id = kthread_self();
    if (id < 0 || id >= KTHREAD_MAX || index < 0 || index >= 8) {
        return 0;
    }
    return g_th[id].tls[index];
}

void kthread_tls_set(int index, void *v) {
    int id = kthread_self();
    if (id < 0 || id >= KTHREAD_MAX || index < 0 || index >= 8) {
        return;
    }
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
    while (c->seq == s) {
        if (cpu_online_count < 2u) {
            job_worker_once(kt_cpu());
        } else {
            __asm__ volatile ("pause");
        }
    }
    kmutex_lock(m);
}

void kcond_signal(KCond *c) {
    c->seq++;
}
