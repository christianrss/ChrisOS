#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "job.h"
#include "kthread.h"

extern void host_set_cpu(uint32_t id);
extern volatile uint32_t cpu_online_count;

void mm_tlb_poll(void) {}

void *kmalloc(uint64_t size) {
    void *p;
    if (size == 0)
        return 0;
    p = malloc((size_t)size);
    return p;
}

void kfree(void *ptr) {
    free(ptr);
}

static volatile int g_stop;
static int g_fail;
static int g_ran[32];
static int g_tls_ok[32];
static int g_self_ok[32];

static int recurse(int n, uint32_t stamp) {
    volatile uint32_t slot[8];
    int i;
    for (i = 0; i < 8; ++i)
        slot[i] = stamp;
    if (n > 0 && recurse(n - 1, stamp) != 0)
        return -1;
    for (i = 0; i < 8; ++i) {
        if (slot[i] != stamp)
            return -1;
    }
    return 0;
}

static void thread_fn(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    int self = kthread_self();
    volatile uint32_t canary[32];
    int i;
    for (i = 0; i < 32; ++i)
        canary[i] = 0xC0FFEEu ^ id;
    if (recurse(6, id + 3u) != 0)
        g_fail = 1;
    for (i = 0; i < 32; ++i) {
        if (canary[i] != (0xC0FFEEu ^ id))
            g_fail = 1;
    }
    kthread_tls_set(0, (void *)(uintptr_t)(id + 100u));
    if ((uint32_t)(uintptr_t)kthread_tls(0) != id + 100u)
        g_tls_ok[id] = 0;
    else
        g_tls_ok[id] = 1;
    g_self_ok[id] = self >= 0;
    g_ran[id] = 1;
}

static void *worker(void *arg) {
    uint32_t cpu = (uint32_t)(uintptr_t)arg;
    host_set_cpu(cpu);
    while (!g_stop) {
        job_worker_once(cpu);
    }
    return 0;
}

int main(void) {
    pthread_t th[4];
    int ids[24];
    int i;
    host_set_cpu(0);
    cpu_online_count = 4u;
    job_init();
    for (i = 0; i < 4; ++i) {
        if (pthread_create(&th[i], 0, worker, (void *)(uintptr_t)(i + 1)) != 0)
            return 1;
    }
    for (i = 0; i < 24; ++i) {
        ids[i] = kthread_create(thread_fn, (void *)(uintptr_t)i);
        if (ids[i] < 0) {
            fprintf(stderr, "kthread_create %d\n", i);
            return 1;
        }
    }
    for (i = 0; i < 24; ++i)
        kthread_join(ids[i]);
    g_stop = 1;
    for (i = 0; i < 4; ++i)
        pthread_join(th[i], 0);
    for (i = 0; i < 24; ++i) {
        if (!g_ran[i] || !g_tls_ok[i] || !g_self_ok[i]) {
            fprintf(stderr, "thread %d ran=%d tls=%d self=%d\n", i, g_ran[i],
                    g_tls_ok[i], g_self_ok[i]);
            return 1;
        }
    }
    if (g_fail)
        return 1;
    if (kthread_create(0, 0) != -1) {
        fprintf(stderr, "null fn should fail\n");
        return 1;
    }
    puts("test_kthread_smp: ok");
    return 0;
}
