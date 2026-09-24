#include <stdio.h>
#include <stdint.h>
#include "job.h"

volatile uint32_t cpu_online_count = 1u;

void mm_tlb_poll(void) {}
void apic_enable_local(void) {}

void panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message ? message : "");
}

void serial_puts(const char *text) { (void)text; }
void serial_write_u64(uint64_t value) { (void)value; }
void serial_write_hex(uint64_t value) { (void)value; }

uint32_t smp_current_cpu(void) { return 0u; }

static int g_ran;

static void marker(void *arg, uint32_t cpu) {
    (void)arg;
    (void)cpu;
    g_ran++;
}

int main(void) {
    uint32_t i;
    int extra;
    job_init();
    for (i = 0; i < JOB_QUEUE_CAP; ++i) {
        if (!job_submit(marker, 0)) {
            fprintf(stderr, "queue filled early at %u\n", i);
            return 1;
        }
    }
    extra = job_submit(marker, 0);
    if (extra != 0) {
        fprintf(stderr, "full queue returned success\n");
        return 1;
    }
    for (i = 0; i < JOB_QUEUE_CAP; ++i)
        job_worker_once(0);
    if (g_ran != (int)JOB_QUEUE_CAP) {
        fprintf(stderr, "drained %d\n", g_ran);
        return 1;
    }
    if (!job_submit(marker, 0)) {
        fprintf(stderr, "submit after drain failed\n");
        return 1;
    }
    job_worker_once(0);
    if (g_ran != (int)JOB_QUEUE_CAP + 1) {
        fprintf(stderr, "post-drain run %d\n", g_ran);
        return 1;
    }
    puts("test_job_saturate: ok");
    return 0;
}
