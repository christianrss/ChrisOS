#include "apic.h"
#include "mm.h"

static volatile uint32_t *g_lapic;
static int g_apic_on;

void apic_eoi(void) {
    if (g_apic_on && g_lapic) {
        g_lapic[0xB0u / 4u] = 0u;
    }
}

int apic_ready(void) {
    return g_apic_on;
}

void apic_init(void) {
    if (g_apic_on) {
        return;
    }
    /* LAPIC mapped in mm_selftest; leave software-disabled until IOAPIC (fase7). */
    g_lapic = (volatile uint32_t *)mm_lapic_virt();
    g_apic_on = 0;
}
