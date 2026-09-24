#include "irq.h"
#include "apic.h"
#include "panic.h"
#include "port.h"
#include "proc.h"
#include "serial.h"
#include "syscall.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA    0xa1
#define PIC_EOI      0x20

static irq_handler handlers[16];
static uint32_t irq_hits[16];

void pic_init(void) {
    unsigned int index;

    __asm__ volatile ("cli");
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);

    for (index = 0; index < 16; ++index) {
        handlers[index] = 0;
    }
}

void pic_set_mask(uint8_t irq, bool masked) {
    uint16_t port;
    uint8_t bit;
    uint8_t value;

    if (irq >= 16) {
        return;
    }
    port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    bit = irq < 8 ? irq : (uint8_t)(irq - 8);
    value = inb(port);
    if (masked) {
        value = (uint8_t)(value | (uint8_t)(1u << bit));
    } else {
        value = (uint8_t)(value & (uint8_t)~(1u << bit));
    }
    outb(port, value);

    if (!masked && irq >= 8) {
        outb(PIC1_DATA, (uint8_t)(inb(PIC1_DATA) & (uint8_t)~(1u << 2)));
    }
}

void irq_set_handler(uint8_t irq, irq_handler handler) {
    if (irq < 16) {
        handlers[irq] = handler;
    }
}

void irq_eoi(uint8_t irq) {
    if (apic_ready()) {
        apic_eoi();
    }
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void irq_dispatch(struct irq_frame *frame) {
    uint8_t irq;

    if (frame->vector == 0x80u) {
        syscall_dispatch(frame);
        return;
    }
    if (frame->vector == 14u) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        if (proc_fault_demand(proc_current(), cr2)) {
            return;
        }
        if ((frame->cs & 3u) != 0u) {
            panic_user_fault(frame, cr2);
            return;
        }
    }
    if (frame->vector < 32) {
        panic_exception(frame->vector, frame->error, frame->rip);
    }
    if (frame->vector >= 48) {
        return;
    }

    irq = (uint8_t)(frame->vector - 32);
    /* IRQ 0 is the 60 Hz timer. Counting it out would stop the desktop. */
    if (irq > 0 && irq < 16 && ++irq_hits[irq] == 10000u) {
        /* A line that never drops livelocks the boot before the desktop. */
        pic_set_mask(irq, true);
        serial_puts("irq storm ");
        serial_write_u64(irq);
        serial_puts("\n");
    }
    if (handlers[irq] != 0) {
        handlers[irq](frame);
    }
    irq_eoi(irq);
}
