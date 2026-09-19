#ifndef CHRIS_SMP_H
#define CHRIS_SMP_H

#include <stdint.h>

#define AP_STACK_PAGES      4u
#define AP_STACK_VIRT_BASE  0xffffffff92000000ull
#define SMP_MAX_APS         8u

extern volatile uint32_t cpu_online_count;
extern uint64_t kernel_cr3;

void smp_init(void);
uint32_t smp_cpu_count(void);

#endif
