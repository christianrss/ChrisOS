#include "job.h"
#include "apic.h"
#include "bootinfo.h"
#include "mm.h"
#include "panic.h"
#include "serial.h"
#include "smp.h"
#include "spin.h"

typedef struct {
    JobFn fn;
    void *arg;
} Job;

static Job g_queue[JOB_QUEUE_CAP];
static uint32_t g_q_head;
static uint32_t g_q_tail;
static uint32_t g_q_count;
static Spinlock g_q_lock;
static volatile uint32_t g_completed;
static volatile uint32_t g_inflight;

void job_init(void) {
    spin_init(&g_q_lock);
    g_q_head = 0u;
    g_q_tail = 0u;
    g_q_count = 0u;
    g_completed = 0u;
    g_inflight = 0u;
}

int job_submit(JobFn fn, void *arg) {
    if (!fn) {
        return 0;
    }
    spin_lock(&g_q_lock);
    if (g_q_count == JOB_QUEUE_CAP) {
        spin_unlock(&g_q_lock);
        return 0;
    }
    g_queue[g_q_tail].fn = fn;
    g_queue[g_q_tail].arg = arg;
    g_q_tail = (g_q_tail + 1u) % JOB_QUEUE_CAP;
    g_q_count += 1u;
    atomic_add_u32(&g_inflight, 1u);
    spin_unlock(&g_q_lock);
    return 1;
}

void job_worker_once(uint32_t cpu_index) {
    Job job;

    mm_tlb_poll();
    job.fn = 0;
    job.arg = 0;
    spin_lock(&g_q_lock);
    if (g_q_count > 0u) {
        job = g_queue[g_q_head];
        g_q_head = (g_q_head + 1u) % JOB_QUEUE_CAP;
        g_q_count -= 1u;
    }
    spin_unlock(&g_q_lock);
    if (job.fn) {
        job.fn(job.arg, cpu_index);
        atomic_add_u32(&g_completed, 1u);
        atomic_add_u32(&g_inflight, (uint32_t)-1);
    }
}

static volatile uint32_t g_ap_irq_enable;

void smp_release_ap_irqs(void) {
    g_ap_irq_enable = 1u;
}

void job_worker_forever(uint32_t cpu_index) {
    int irqs = 0;
    for (;;) {
        if (!irqs && g_ap_irq_enable) {
            if (!bootflag_noapic()) {
                apic_enable_local();
            }
            __asm__ volatile ("sti");
            irqs = 1;
        }
        job_worker_once(cpu_index);
        __asm__ volatile ("pause");
    }
}

void job_wait_idle(void) {
    while (atomic_add_u32(&g_inflight, 0u) != 0u) {
        job_worker_once(0);
        __asm__ volatile ("pause");
    }
}

uint32_t job_completed(void) {
    return g_completed;
}

static volatile uint32_t g_job_sum;

static void add_one(void *arg, uint32_t cpu_index) {
    (void)arg;
    (void)cpu_index;
    atomic_add_u32(&g_job_sum, 1u);
}

void smp_job_selftest(void) {
    uint32_t i;
    uint32_t wave;

    g_job_sum = 0u;
    if (cpu_online_count < 2u) {
        serial_puts("job selftest: skipped (BSP only)\n");
        return;
    }
    for (i = 0; i < 16u; ++i) {
        uint32_t spins = 0u;
        while (!job_submit(add_one, 0)) {
            job_worker_once(0);
            if (++spins > 1000000u) {
                panic("job_submit failed");
            }
        }
    }
    job_wait_idle();
    if (g_job_sum != 16u) {
        serial_puts("job_sum=");
        serial_write_u64(g_job_sum);
        serial_puts(" expected 16\n");
        panic("job_sum");
    }
    for (wave = 0u; wave < 32u; ++wave) {
        g_job_sum = 0u;
        for (i = 0u; i < 128u; ++i) {
            uint32_t spins = 0u;
            while (!job_submit(add_one, 0)) {
                job_worker_once(0);
                if (++spins > 1000000u) {
                    panic("job_submit failed");
                }
            }
        }
        job_wait_idle();
        if (g_job_sum != 128u) {
            serial_puts("job_sum=");
            serial_write_u64(g_job_sum);
            serial_puts(" expected 128\n");
            panic("job_sum");
        }
    }
    serial_puts("job selftest ok sum=16\n");
}
