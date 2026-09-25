#ifndef CHRISOS_TLB_PROTO_H
#define CHRISOS_TLB_PROTO_H

#include <stdint.h>

/* Formal TLB shootdown membership. A CPU is either absent, online, or
 * fenced. Fenced CPUs are not online: a later shootdown does not wait
 * for them, and their frames stay unreused until that CPU has halted. */

#define TLB_CPU_CAP    16u
#define TLB_CPU_ABSENT 0u
#define TLB_CPU_ONLINE 1u
#define TLB_CPU_FENCED 2u

typedef struct TlbCpu {
    volatile uint32_t state;
    volatile uint64_t seen;
    volatile uint64_t heartbeat;
    volatile uint32_t halted;
    /* 1 after this CPU has invalidated the generation that fenced it. */
    volatile uint32_t flushed;
} TlbCpu;

typedef struct TlbWorld {
    TlbCpu cpu[TLB_CPU_CAP];
    volatile uint64_t gen;
    volatile uint64_t virt;
    volatile uint64_t bytes;
    uint64_t hb_snap[TLB_CPU_CAP];
    uint32_t quiet[TLB_CPU_CAP];
    uint32_t quiet_limit;
    uint32_t published;
    int reuse_ok;
} TlbWorld;

void tlb_world_init(TlbWorld *world, uint32_t quiet_limit);
void tlb_cpu_online(TlbWorld *world, uint32_t cpu);
uint32_t tlb_cpu_state(const TlbWorld *world, uint32_t cpu);
uint32_t tlb_online_count(const TlbWorld *world);
void tlb_cpu_heartbeat(TlbWorld *world, uint32_t cpu);
void tlb_cpu_halted(TlbWorld *world, uint32_t cpu);
/* NMI model: invalidate the published generation, then halt. The CPU
 * stays fenced. Reuse requires both. */
void tlb_cpu_stop(TlbWorld *world, uint32_t cpu);
void tlb_publish(TlbWorld *world, uint32_t self, uint64_t virt, uint64_t bytes);
int tlb_pending(const TlbWorld *world, uint32_t cpu, uint64_t *virt, uint64_t *bytes);
void tlb_ack(TlbWorld *world, uint32_t cpu);
uint32_t tlb_ipi_targets(const TlbWorld *world, uint32_t self, uint32_t *out, uint32_t cap);
/* 0: every online CPU has acked this generation.
 * 1: still waiting.
 * 2: every CPU that stayed silent past its quiet budget was fenced.
 * *newly_fenced has one bit per CPU fenced by this call. */
int tlb_wait_step(TlbWorld *world, uint32_t self, uint32_t *newly_fenced);
/* Fence every other online CPU that has not acked, ignoring the quiet
 * budget. Returns a bit per CPU fenced by this call. */
uint32_t tlb_fence_unacked(TlbWorld *world, uint32_t self);
int tlb_reuse_ok(const TlbWorld *world);

void tlb_runtime_init(void);
void tlb_runtime_online(uint32_t cpu);
int tlb_runtime_is_fenced(uint32_t cpu);
void tlb_runtime_heartbeat(uint32_t cpu);
void tlb_runtime_mark_halted(uint32_t cpu);
void tlb_runtime_publish(uint32_t self, uint64_t virt, uint64_t bytes);
int tlb_runtime_pending(uint32_t cpu, uint64_t *virt, uint64_t *bytes);
void tlb_runtime_ack(uint32_t cpu);
uint32_t tlb_runtime_ipi_targets(uint32_t self, uint32_t *out, uint32_t cap);
int tlb_runtime_wait_step(uint32_t self, uint32_t *newly_fenced);
uint32_t tlb_runtime_fence_unacked(uint32_t self);
int tlb_runtime_reuse_ok(void);
/* One serial line: tag plus each live CPU's state, seen, and heartbeat. */
void tlb_runtime_log(const char *tag);

#endif
