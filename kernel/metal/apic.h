#ifndef CHRIS_APIC_H
#define CHRIS_APIC_H

#include <stdint.h>

void apic_init(void);
void apic_eoi(void);
int apic_ready(void);
/* Software-enable this CPU's LAPIC. PIC lines stay on the BSP. */
void apic_enable_local(void);
/* Fixed delivery. Returns 0 when the ICR delivery bit clears. */
int apic_ipi(uint32_t dest_lapic, uint8_t vector);
/* NMI delivery. IF does not block it. Returns 0 when the ICR bit clears. */
int apic_ipi_nmi(uint32_t dest_lapic);

#endif
