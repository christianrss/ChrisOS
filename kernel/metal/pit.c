#include "pit.h"
#include "irq.h"
#include "port.h"
#include "proc.h"

#define PIT_INPUT_HZ 1193182u

volatile uint64_t ticks;

static void pit_irq(struct irq_frame *frame) {
    (void)frame;
    ++ticks;
    proc_on_tick();
}

bool pit_init(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0) {
        return false;
    }
    divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0 || divisor > 0xffffu) {
        return false;
    }

    ticks = 0;
    irq_set_handler(0, pit_irq);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
    pic_set_mask(0, false);
    return true;
}

uint64_t pit_ticks(void) {
    return ticks;
}
