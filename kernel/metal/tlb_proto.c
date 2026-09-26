#include "tlb_proto.h"

#ifdef __freestanding__
#include "serial.h"
#endif

/* Quiet polls with no heartbeat before a silent CPU is fenced. A polling
 * CPU acks from its worker loop, so this budget is only the stuck case. */
#define TLB_RUNTIME_QUIET 2000000u

static TlbWorld g_tlb;
static int g_tlb_ready;

static void compiler_barrier(void) {
    __asm__ volatile ("" ::: "memory");
}

static void recompute_reuse(TlbWorld *world) {
    uint32_t i;

    if (!world->published) {
        world->reuse_ok = 1;
        return;
    }
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (world->cpu[i].state == TLB_CPU_ONLINE &&
            world->cpu[i].seen != world->gen) {
            world->reuse_ok = 0;
            return;
        }
        if (world->cpu[i].state == TLB_CPU_FENCED &&
            (world->cpu[i].halted == 0u || world->cpu[i].flushed == 0u)) {
            world->reuse_ok = 0;
            return;
        }
    }
    world->reuse_ok = 1;
}

void tlb_world_init(TlbWorld *world, uint32_t quiet_limit) {
    uint32_t i;

    if (!world) {
        return;
    }
    for (i = 0; i < TLB_CPU_CAP; i++) {
        world->cpu[i].state = TLB_CPU_ABSENT;
        world->cpu[i].seen = 0u;
        world->cpu[i].heartbeat = 0u;
        world->cpu[i].halted = 0u;
        world->cpu[i].flushed = 0u;
        world->hb_snap[i] = 0u;
        world->quiet[i] = 0u;
    }
    world->gen = 0u;
    world->virt = 0u;
    world->bytes = 0u;
    world->quiet_limit = quiet_limit == 0u ? 1u : quiet_limit;
    world->published = 0u;
    world->reuse_ok = 1;
}

void tlb_cpu_online(TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return;
    }
    if (world->cpu[cpu].state == TLB_CPU_ABSENT) {
        world->cpu[cpu].state = TLB_CPU_ONLINE;
    }
}

uint32_t tlb_cpu_state(const TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return TLB_CPU_ABSENT;
    }
    return world->cpu[cpu].state;
}

uint32_t tlb_online_count(const TlbWorld *world) {
    uint32_t i;
    uint32_t n = 0u;

    if (!world) {
        return 0u;
    }
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (world->cpu[i].state == TLB_CPU_ONLINE) {
            n++;
        }
    }
    return n;
}

void tlb_cpu_heartbeat(TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return;
    }
    if (world->cpu[cpu].state == TLB_CPU_ABSENT) {
        return;
    }
    world->cpu[cpu].heartbeat++;
}

void tlb_cpu_halted(TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return;
    }
    if (world->cpu[cpu].state != TLB_CPU_FENCED) {
        return;
    }
    world->cpu[cpu].halted = 1u;
    recompute_reuse(world);
}

void tlb_cpu_stop(TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return;
    }
    if (world->cpu[cpu].state != TLB_CPU_FENCED) {
        return;
    }
    world->cpu[cpu].seen = world->gen;
    world->cpu[cpu].flushed = 1u;
    world->cpu[cpu].halted = 1u;
    recompute_reuse(world);
}

void tlb_publish(TlbWorld *world, uint32_t self, uint64_t virt, uint64_t bytes) {
    uint32_t i;
    uint64_t gen;

    if (!world) {
        return;
    }
    if (self >= TLB_CPU_CAP) {
        self = 0u;
    }
    world->virt = virt;
    world->bytes = bytes;
    compiler_barrier();
    gen = world->gen + 1u;
    if (gen == 0u) {
        gen = 1u;
    }
    world->gen = gen;
    world->published = 1u;
    for (i = 0; i < TLB_CPU_CAP; i++) {
        world->hb_snap[i] = world->cpu[i].heartbeat;
        world->quiet[i] = 0u;
    }
    tlb_cpu_online(world, self);
    world->cpu[self].seen = gen;
    recompute_reuse(world);
}

int tlb_pending(const TlbWorld *world, uint32_t cpu, uint64_t *virt, uint64_t *bytes) {
    if (!world || cpu >= TLB_CPU_CAP || !world->published) {
        return 0;
    }
    if (world->cpu[cpu].state == TLB_CPU_ABSENT) {
        return 0;
    }
    if (world->cpu[cpu].seen == world->gen) {
        return 0;
    }
    if (virt) {
        *virt = world->virt;
    }
    if (bytes) {
        *bytes = world->bytes;
    }
    return 1;
}

void tlb_ack(TlbWorld *world, uint32_t cpu) {
    if (!world || cpu >= TLB_CPU_CAP) {
        return;
    }
    if (world->cpu[cpu].state == TLB_CPU_ABSENT) {
        return;
    }
    world->cpu[cpu].seen = world->gen;
    if (world->cpu[cpu].state == TLB_CPU_FENCED) {
        world->cpu[cpu].flushed = 1u;
    }
    recompute_reuse(world);
}

uint32_t tlb_ipi_targets(const TlbWorld *world, uint32_t self, uint32_t *out, uint32_t cap) {
    uint32_t i;
    uint32_t n = 0u;

    if (!world || !out || cap == 0u) {
        return 0u;
    }
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (i == self) {
            continue;
        }
        if (world->cpu[i].state != TLB_CPU_ONLINE) {
            continue;
        }
        if (world->cpu[i].seen == world->gen) {
            continue;
        }
        if (n >= cap) {
            break;
        }
        out[n++] = i;
    }
    return n;
}

static int fence_cpu(TlbWorld *world, uint32_t cpu) {
    if (cpu >= TLB_CPU_CAP) {
        return 0;
    }
    if (world->cpu[cpu].state != TLB_CPU_ONLINE) {
        return 0;
    }
    world->cpu[cpu].state = TLB_CPU_FENCED;
    world->cpu[cpu].halted = 0u;
    world->cpu[cpu].flushed = 0u;
    return 1;
}

int tlb_wait_step(TlbWorld *world, uint32_t self, uint32_t *newly_fenced) {
    uint32_t i;
    int pending;
    int fenced_now;

    if (newly_fenced) {
        *newly_fenced = 0u;
    }
    if (!world) {
        return 0;
    }
    if (self >= TLB_CPU_CAP) {
        self = 0u;
    }
    pending = 0;
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (world->cpu[i].state != TLB_CPU_ONLINE) {
            continue;
        }
        if (world->cpu[i].seen == world->gen) {
            continue;
        }
        pending = 1;
        if (world->cpu[i].heartbeat != world->hb_snap[i]) {
            world->hb_snap[i] = world->cpu[i].heartbeat;
            world->quiet[i] = 0u;
        } else if (world->quiet[i] < 0xffffffffu) {
            world->quiet[i]++;
        }
    }
    if (!pending) {
        recompute_reuse(world);
        return 0;
    }
    fenced_now = 0;
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (i == self) {
            continue;
        }
        if (world->cpu[i].state != TLB_CPU_ONLINE) {
            continue;
        }
        if (world->cpu[i].seen == world->gen) {
            continue;
        }
        if (world->quiet[i] <= world->quiet_limit) {
            continue;
        }
        if (fence_cpu(world, i)) {
            fenced_now = 1;
            if (newly_fenced) {
                *newly_fenced |= (1u << i);
            }
        }
    }
    recompute_reuse(world);
    if (!fenced_now) {
        return 1;
    }
    for (i = 0; i < TLB_CPU_CAP; i++) {
        if (world->cpu[i].state == TLB_CPU_ONLINE &&
            world->cpu[i].seen != world->gen) {
            return 1;
        }
    }
    return 2;
}

uint32_t tlb_fence_unacked(TlbWorld *world, uint32_t self) {
    uint32_t i;
    uint32_t mask = 0u;

    if (!world) {
        return 0u;
    }
    if (self >= TLB_CPU_CAP) {
        self = 0u;
    }
    for (i = 0u; i < TLB_CPU_CAP; i++) {
        if (i == self) {
            continue;
        }
        if (world->cpu[i].state != TLB_CPU_ONLINE) {
            continue;
        }
        if (world->cpu[i].seen == world->gen) {
            continue;
        }
        if (fence_cpu(world, i)) {
            mask |= (1u << i);
        }
    }
    recompute_reuse(world);
    return mask;
}

int tlb_reuse_ok(const TlbWorld *world) {
    if (!world) {
        return 0;
    }
    return world->reuse_ok;
}

void tlb_runtime_init(void) {
    tlb_world_init(&g_tlb, TLB_RUNTIME_QUIET);
    tlb_cpu_online(&g_tlb, 0u);
    g_tlb_ready = 1;
}

void tlb_runtime_online(uint32_t cpu) {
    if (!g_tlb_ready) {
        return;
    }
    tlb_cpu_online(&g_tlb, cpu);
}

int tlb_runtime_is_fenced(uint32_t cpu) {
    if (!g_tlb_ready) {
        return 0;
    }
    return tlb_cpu_state(&g_tlb, cpu) == TLB_CPU_FENCED;
}

void tlb_runtime_heartbeat(uint32_t cpu) {
    if (!g_tlb_ready) {
        return;
    }
    tlb_cpu_heartbeat(&g_tlb, cpu);
}

void tlb_runtime_mark_halted(uint32_t cpu) {
    if (!g_tlb_ready) {
        return;
    }
    tlb_cpu_halted(&g_tlb, cpu);
}

void tlb_runtime_publish(uint32_t self, uint64_t virt, uint64_t bytes) {
    if (!g_tlb_ready) {
        tlb_runtime_init();
    }
    tlb_publish(&g_tlb, self, virt, bytes);
}

int tlb_runtime_pending(uint32_t cpu, uint64_t *virt, uint64_t *bytes) {
    if (!g_tlb_ready) {
        return 0;
    }
    return tlb_pending(&g_tlb, cpu, virt, bytes);
}

void tlb_runtime_ack(uint32_t cpu) {
    if (!g_tlb_ready) {
        return;
    }
    tlb_ack(&g_tlb, cpu);
}

uint32_t tlb_runtime_ipi_targets(uint32_t self, uint32_t *out, uint32_t cap) {
    if (!g_tlb_ready) {
        return 0u;
    }
    return tlb_ipi_targets(&g_tlb, self, out, cap);
}

int tlb_runtime_wait_step(uint32_t self, uint32_t *newly_fenced) {
    if (!g_tlb_ready) {
        return 0;
    }
    return tlb_wait_step(&g_tlb, self, newly_fenced);
}

uint32_t tlb_runtime_fence_unacked(uint32_t self) {
    if (!g_tlb_ready) {
        return 0u;
    }
    return tlb_fence_unacked(&g_tlb, self);
}

int tlb_runtime_reuse_ok(void) {
    if (!g_tlb_ready) {
        return 1;
    }
    return tlb_reuse_ok(&g_tlb);
}

#ifdef __freestanding__
void tlb_runtime_log(const char *tag) {
    uint32_t i;

    serial_puts(tag ? tag : "tlb");
    serial_puts(" gen=");
    serial_write_u64(g_tlb.gen);
    serial_puts(" ready=");
    serial_write_u64((uint64_t)g_tlb_ready);
    for (i = 0u; i < TLB_CPU_CAP; i++) {
        if (g_tlb.cpu[i].state == TLB_CPU_ABSENT) {
            continue;
        }
        serial_puts(" c");
        serial_write_u64(i);
        serial_puts(" st=");
        serial_write_u64(g_tlb.cpu[i].state);
        serial_puts(" seen=");
        serial_write_u64(g_tlb.cpu[i].seen);
        serial_puts(" hb=");
        serial_write_u64(g_tlb.cpu[i].heartbeat);
        serial_puts(" halt=");
        serial_write_u64(g_tlb.cpu[i].halted);
        serial_puts(" fl=");
        serial_write_u64(g_tlb.cpu[i].flushed);
    }
    serial_puts("\n");
}
#endif
