#include "port.h"
#include "irq.h"

void irq_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 4);    outb(0xA1, 2);
    outb(0x21, 1);    outb(0xA1, 1);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);
}

void irq_eoi(int irq) {
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void irq_dispatch(unsigned long vec) {
    if (vec == 32) {
        /* ticks++ */
        irq_eoi(0);
        return;
    }
    if (vec == 33) {
        /* teclado: inb(0x60) */
        irq_eoi(1);
        return;
    }
    if (vec == 44) {
        /* rato: PS/2 01d */
        irq_eoi(12);
        return;
    }
    if (vec >= 32 && vec < 48)
        irq_eoi((int)(vec - 32));
}