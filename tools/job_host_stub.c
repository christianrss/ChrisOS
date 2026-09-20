/* Host stub for kernel/metal/job.h — used by gfx host tests. */
#include "job_host_stub.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define JOB_Q_CAP 512u

typedef struct {
    JobFn fn;
    void *arg;
} HostJob;

static HostJob g_queue[JOB_Q_CAP];
static uint32_t g_head;
static uint32_t g_tail;
static uint32_t g_count;
static volatile uint32_t g_inflight;
static int g_workers = 1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_nonempty = PTHREAD_COND_INITIALIZER;
static volatile int g_draining;

static int dequeue(HostJob *out) {
    if (g_count == 0u) {
        return 0;
    }
    *out = g_queue[g_head];
    g_head = (g_head + 1u) % JOB_Q_CAP;
    g_count -= 1u;
    return 1;
}

static void run_job(const HostJob *job, uint32_t cpu) {
    if (job->fn) {
        job->fn(job->arg, cpu);
    }
}

static void *pool_worker(void *arg) {
    uint32_t cpu = (uint32_t)(uintptr_t)arg;

    for (;;) {
        HostJob job = {0, 0};
        pthread_mutex_lock(&g_lock);
        while (g_count == 0u && g_draining) {
            pthread_cond_wait(&g_nonempty, &g_lock);
        }
        if (!g_draining) {
            pthread_mutex_unlock(&g_lock);
            break;
        }
        if (!dequeue(&job)) {
            pthread_mutex_unlock(&g_lock);
            continue;
        }
        pthread_mutex_unlock(&g_lock);
        run_job(&job, cpu);
        __sync_fetch_and_sub(&g_inflight, 1u);
    }
    return 0;
}

void job_host_set_workers(int n) {
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {
        n = 8;
    }
    g_workers = n;
}

int job_submit(JobFn fn, void *arg) {
    if (!fn) {
        return 0;
    }
    pthread_mutex_lock(&g_lock);
    if (g_count == JOB_Q_CAP) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    g_queue[g_tail].fn = fn;
    g_queue[g_tail].arg = arg;
    g_tail = (g_tail + 1u) % JOB_Q_CAP;
    g_count += 1u;
    g_inflight += 1u;
    pthread_cond_signal(&g_nonempty);
    pthread_mutex_unlock(&g_lock);
    return 1;
}

void job_wait_idle(void) {
    pthread_t threads[8];
    int i;

    if (g_workers <= 1) {
        for (;;) {
            HostJob job = {0, 0};
            pthread_mutex_lock(&g_lock);
            if (!dequeue(&job)) {
                pthread_mutex_unlock(&g_lock);
                break;
            }
            pthread_mutex_unlock(&g_lock);
            run_job(&job, 0);
            __sync_fetch_and_sub(&g_inflight, 1u);
        }
        while (g_inflight != 0u) {
        }
        return;
    }

    g_draining = 1;
    for (i = 0; i < g_workers; ++i) {
        pthread_create(&threads[i], 0, pool_worker, (void *)(uintptr_t)(uint32_t)i);
    }
    while (g_inflight != 0u || g_count != 0u) {
        pthread_mutex_lock(&g_lock);
        pthread_cond_broadcast(&g_nonempty);
        pthread_mutex_unlock(&g_lock);
    }
    g_draining = 0;
    pthread_cond_broadcast(&g_nonempty);
    for (i = 0; i < g_workers; ++i) {
        pthread_join(threads[i], 0);
    }
}

void job_host_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_head = 0u;
    g_tail = 0u;
    g_count = 0u;
    g_inflight = 0u;
    g_draining = 0;
    memset(g_queue, 0, sizeof(g_queue));
    pthread_mutex_unlock(&g_lock);
}
