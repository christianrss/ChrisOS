#include <stdio.h>
#include <string.h>

#include "tlb_proto.h"

static int g_fail;

static void expect(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", what);
        g_fail = 1;
    }
}

static void bring(TlbWorld *world, uint32_t n) {
    uint32_t i;
    tlb_world_init(world, 2u);
    for (i = 0; i < n; i++) {
        tlb_cpu_online(world, i);
    }
}

static void test_all_ack(void) {
    TlbWorld world;
    uint32_t fenced = 0xffffffffu;
    int step;

    bring(&world, 4u);
    tlb_publish(&world, 0u, 0x1000u, 0x3000u);
    expect(!tlb_reuse_ok(&world), "reuse blocked before remote ack");
    tlb_ack(&world, 1u);
    tlb_ack(&world, 2u);
    tlb_ack(&world, 3u);
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 0, "all online acks complete the generation");
    expect(fenced == 0u, "no fence when every online CPU acks");
    expect(tlb_reuse_ok(&world), "reuse after every online ack");
    expect(tlb_online_count(&world) == 4u, "four CPUs stay online");
}

static void test_silent_cpu_is_fenced(void) {
    TlbWorld world;
    uint32_t fenced = 0u;
    int step;
    int i;

    bring(&world, 4u);
    tlb_publish(&world, 0u, 0x2000u, 4096u);
    tlb_ack(&world, 1u);
    tlb_ack(&world, 3u);
    step = 1;
    for (i = 0; i < 8 && step == 1; i++) {
        step = tlb_wait_step(&world, 0u, &fenced);
    }
    expect(step == 2, "silent CPU ends the wait by being fenced");
    expect((fenced & (1u << 2)) != 0u, "CPU 2 is the fenced CPU");
    expect(tlb_cpu_state(&world, 2u) == TLB_CPU_FENCED, "CPU 2 state is fenced");
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_ONLINE, "acking CPU stays online");
    expect(tlb_online_count(&world) == 3u, "online set dropped the silent CPU");
    expect(!tlb_reuse_ok(&world), "fenced CPU blocks reuse until it halts");
    tlb_ack(&world, 2u);
    expect(tlb_cpu_state(&world, 2u) == TLB_CPU_FENCED, "ack does not restore a fenced CPU");
    expect(!tlb_reuse_ok(&world), "ack alone does not allow reuse after a fence");
    tlb_cpu_halted(&world, 2u);
    expect(tlb_reuse_ok(&world), "reuse after the fenced CPU halts");
}

static void test_hole_is_not_a_prefix(void) {
    TlbWorld world;
    uint32_t fenced = 0u;
    uint32_t targets[8];
    uint32_t n;
    int step;
    int i;

    bring(&world, 3u);
    tlb_publish(&world, 0u, 0x3000u, 4096u);
    tlb_ack(&world, 2u);
    step = 1;
    for (i = 0; i < 8 && step == 1; i++) {
        step = tlb_wait_step(&world, 0u, &fenced);
    }
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_FENCED, "middle CPU fenced");
    expect(tlb_cpu_state(&world, 2u) == TLB_CPU_ONLINE, "high CPU stays online");
    tlb_cpu_stop(&world, 1u);

    tlb_publish(&world, 0u, 0x4000u, 4096u);
    n = tlb_ipi_targets(&world, 0u, targets, 8u);
    expect(n == 1u && targets[0] == 2u, "next shootdown asks only the live high CPU");
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 1, "high CPU is still required after a hole in the set");
    expect(!tlb_reuse_ok(&world), "reuse waits for the live high CPU");
    tlb_ack(&world, 2u);
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 0 && tlb_reuse_ok(&world), "reuse after the live high CPU acks");
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_FENCED, "fenced CPU stays out");
}

static void test_halt_without_invlpg_blocks_reuse(void) {
    TlbWorld world;
    uint32_t fenced = 0u;
    int step;
    int i;

    bring(&world, 2u);
    tlb_publish(&world, 0u, 0x6000u, 4096u);
    step = 1;
    for (i = 0; i < 8 && step == 1; i++) {
        step = tlb_wait_step(&world, 0u, &fenced);
    }
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_FENCED, "silent CPU is fenced");
    tlb_cpu_halted(&world, 1u);
    expect(!tlb_reuse_ok(&world), "halt without invlpg does not release frames");
    tlb_ack(&world, 1u);
    expect(tlb_reuse_ok(&world), "reuse after the fenced CPU invalidates and halts");
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_FENCED, "stop does not unfence the CPU");
}

static void test_heartbeat_resets_quiet(void) {
    TlbWorld world;
    uint32_t fenced = 0u;
    int step;

    bring(&world, 2u);
    tlb_publish(&world, 0u, 0x5000u, 4096u);
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 1, "first quiet poll waits");
    tlb_cpu_heartbeat(&world, 1u);
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 1 && fenced == 0u, "a moving heartbeat is not a stuck CPU");
    expect(tlb_cpu_state(&world, 1u) == TLB_CPU_ONLINE, "live CPU stays online");
    tlb_ack(&world, 1u);
    step = tlb_wait_step(&world, 0u, &fenced);
    expect(step == 0 && tlb_reuse_ok(&world), "late ack still completes");
}

int main(void) {
    test_all_ack();
    test_silent_cpu_is_fenced();
    test_hole_is_not_a_prefix();
    test_halt_without_invlpg_blocks_reuse();
    test_heartbeat_resets_quiet();
    if (g_fail) {
        fprintf(stderr, "tlb proto tests failed\n");
        return 1;
    }
    printf("tlb proto tests passed\n");
    return 0;
}
