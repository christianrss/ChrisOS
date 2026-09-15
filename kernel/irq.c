#include "port.h"
#include "irq.h"
#include "panic.h"

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

void irq_dispatch(struct irq_frame *frame) {
    if (frame->vector < 32) {
        panic_exception(frame->vector, frame->error, frame->rip);
    }
}