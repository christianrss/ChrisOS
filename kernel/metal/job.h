#ifndef CHRIS_JOB_H
#define CHRIS_JOB_H

#include <stdint.h>

#define JOB_QUEUE_CAP 32u

typedef void (*JobFn)(void *arg, uint32_t cpu_index);

void job_init(void);
int job_submit(JobFn fn, void *arg);
void job_worker_once(uint32_t cpu_index);
void job_worker_forever(uint32_t cpu_index);
void job_wait_idle(void);
uint32_t job_completed(void);
void smp_job_selftest(void);

#endif
