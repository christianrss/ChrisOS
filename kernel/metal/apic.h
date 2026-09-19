#ifndef CHRIS_APIC_H
#define CHRIS_APIC_H

void apic_init(void);
void apic_eoi(void);
int apic_ready(void);

#endif
