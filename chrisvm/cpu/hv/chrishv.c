#include "machine/machine.h"

#include <stdio.h>

/* ChrisHV is the hardware-assisted backend. This round only reserves the
 * seam. There is no VMX, no SVM, and no KVM. */

static int hv_fail(void) {
    fprintf(stderr, "chrishv: not implemented (no VMX/SVM in this round)\n");
    return -1;
}

static int hv_init(ChrisMachine *machine) {
    (void)machine;
    return hv_fail();
}

static int hv_create(ChrisMachine *machine, unsigned cpu_id) {
    (void)machine;
    (void)cpu_id;
    return -1;
}

static int hv_reset(ChrisCpu *cpu) {
    (void)cpu;
    return -1;
}

static int hv_run(ChrisCpu *cpu, uint64_t max_steps) {
    (void)cpu;
    (void)max_steps;
    return -1;
}

static void hv_inject(ChrisCpu *cpu, uint8_t vector) {
    (void)cpu;
    (void)vector;
}

static void hv_get(const ChrisCpu *cpu, ChrisArchitectureState *out) {
    (void)cpu;
    (void)out;
}

static void hv_set(ChrisCpu *cpu, const ChrisArchitectureState *in) {
    (void)cpu;
    (void)in;
}

static void hv_tlb(ChrisCpu *cpu) {
    (void)cpu;
}

static void hv_shutdown(ChrisCpu *cpu) {
    (void)cpu;
}

const ChrisCpuBackend chrishv_backend = {
    "chrishv", hv_init, hv_create, hv_reset, hv_run, hv_inject, hv_get, hv_set, hv_tlb, hv_shutdown,
};
