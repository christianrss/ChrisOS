#ifndef CHRIS_SMP_H
#define CHRIS_SMP_H

#include <stdint.h>

#define AP_STACK_PAGES      4u
#define AP_STACK_VIRT_BASE  0xffffffff92000000ull
#define SMP_MAX_APS         8u
#define SMP_CPU_CAP         16u

extern volatile uint32_t cpu_online_count;
extern uint64_t kernel_cr3;

void smp_init(void);
uint32_t smp_cpu_count(void);
/* Index of the CPU executing this call. 0 until SMP bookkeeping is live.
 * Stable for the life of the CPU; not a shared global. */
uint32_t smp_current_cpu(void);
/* LAPIC id recorded at boot for this cpu index. Returns 0 and *known=0
 * when the slot was never published. */
uint32_t smp_lapic_of(uint32_t cpu, int *known);

#endif
