#include "ioapic.h"
#include "serial.h"

void ioapic_init(void) {
    serial_puts("ioapic: PIC still routes IRQ (full IOAPIC in fase7 PASSO 01)\n");
}
