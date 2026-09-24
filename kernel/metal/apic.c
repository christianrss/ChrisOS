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

void apic_enable_local(void) {
    volatile uint32_t *lapic = (volatile uint32_t *)mm_lapic_virt();
    if (!lapic) {
        return;
    }
    /* Spurious vector 0xFF, software enable. Does not touch the IMCR,
     * so the i8259 path used by the timer and ATA stays in place. */
    lapic[0xF0u / 4u] = 0x100u | 0xFFu;
    g_lapic = lapic;
    g_apic_on = 1;
}

int apic_ipi(uint32_t dest_lapic, uint8_t vector) {
    uint32_t spins;
    if (!g_apic_on || !g_lapic) {
        return -1;
    }
    g_lapic[0x310u / 4u] = dest_lapic << 24;
    g_lapic[0x300u / 4u] = (uint32_t)vector | (1u << 14);
    for (spins = 0u; spins < 100000u; spins++) {
        if ((g_lapic[0x300u / 4u] & (1u << 12)) == 0u) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
    return -1;
}

void apic_init(void) {
    if (g_apic_on) {
        return;
    }
    /* LAPIC mapped in mm_selftest; leave software-disabled until IOAPIC (fase7). */
    g_lapic = (volatile uint32_t *)mm_lapic_virt();
    g_apic_on = 0;
}
