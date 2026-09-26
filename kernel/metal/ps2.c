#include "ps2.h"
#include "input.h"
#include "irq.h"
#include "port.h"
#include "serial.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64
#define TIMEOUT    1000000u

static bool wait_input_empty(void) {
    uint32_t timeout;
    for (timeout = 0; timeout < TIMEOUT; ++timeout) {
        if ((inb(PS2_STATUS) & 0x02u) == 0) {
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static bool wait_output_full(void) {
    uint32_t timeout;
    for (timeout = 0; timeout < TIMEOUT; ++timeout) {
        if ((inb(PS2_STATUS) & 0x01u) != 0) {
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static void flush_output(void) {
    uint32_t guard = 0u;
    while ((inb(PS2_STATUS) & 0x01u) != 0 && guard < 32u) {
        (void)inb(PS2_DATA);
        guard += 1u;
    }
}

static void ps2_delay(void) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        (void)inb(PS2_STATUS);
    }
}

static bool controller_command(uint8_t command) {
    if (!wait_input_empty()) {
        return false;
    }
    outb(PS2_CMD, command);
    ps2_delay();
    return true;
}

static bool controller_write_data(uint8_t value) {
    if (!wait_input_empty()) {
        return false;
    }
    outb(PS2_DATA, value);
    ps2_delay();
    return true;
}

static bool controller_read_data(uint8_t *value) {
    if (!wait_output_full()) {
        return false;
    }
    *value = inb(PS2_DATA);
    return true;
}

static bool device_command(bool mouse, uint8_t command) {
    uint8_t response;

    if (mouse && !controller_command(0xd4)) {
        return false;
    }
    if (!controller_write_data(command)) {
        return false;
    }
    if (!controller_read_data(&response)) {
        return false;
    }
    return response == 0xfa;
}

static void keyboard_irq(struct irq_frame *frame) {
    uint8_t value;
    (void)frame;
    if ((inb(PS2_STATUS) & 0x01u) == 0) {
        return;
    }
    value = inb(PS2_DATA);
    input_keyboard_irq(value);
}

static void mouse_irq(struct irq_frame *frame) {
    uint8_t value;
    (void)frame;
    if ((inb(PS2_STATUS) & 0x21u) != 0x21u) {
        return;
    }
    value = inb(PS2_DATA);
    input_mouse_irq_byte(value);
}

static bool ps2_try_init(int *mouse_ok) {
    uint8_t config;
    uint8_t result;

    *mouse_ok = 0;
    if (!controller_command(0xad) || !controller_command(0xa7)) {
        return false;
    }
    flush_output();

    if (!controller_command(0x20) || !controller_read_data(&config)) {
        return false;
    }
    config = (uint8_t)((config & (uint8_t)~0x03u) | 0x40u);
    if (!controller_command(0x60) || !controller_write_data(config)) {
        return false;
    }

    if (!controller_command(0xaa) ||
        !controller_read_data(&result) || result != 0x55) {
        return false;
    }
    if (!controller_command(0xab) ||
        !controller_read_data(&result) || result != 0x00) {
        return false;
    }

    if (controller_command(0xa9) &&
        controller_read_data(&result) && result == 0x00) {
        *mouse_ok = 1;
    }

    if (!controller_command(0xae)) {
        return false;
    }
    if (*mouse_ok && !controller_command(0xa8)) {
        *mouse_ok = 0;
    }
    if (!controller_command(0x60) ||
        !controller_write_data((uint8_t)(config | 0x01u | (*mouse_ok ? 0x02u : 0u)))) {
        return false;
    }

    if (!device_command(false, 0xf4)) {
        return false;
    }
    if (*mouse_ok) {
        if (!device_command(true, 0xf6) || !device_command(true, 0xf4)) {
            *mouse_ok = 0;
        }
    }
    return true;
}

bool ps2_init(void) {
    uint32_t attempt;
    int mouse_ok = 0;

    for (attempt = 0; attempt < 8u; attempt++) {
        if (attempt > 0u) {
            uint32_t i;
            for (i = 0; i < 200000u; i++) {
                __asm__ volatile ("pause");
            }
        }
        if (ps2_try_init(&mouse_ok)) {
            irq_set_handler(1, keyboard_irq);
            pic_set_mask(1, false);
            if (mouse_ok) {
                irq_set_handler(12, mouse_irq);
                pic_set_mask(12, false);
                serial_puts("ps2 mouse on\n");
            } else {
                serial_puts("ps2 mouse off\n");
            }
            return true;
        }
    }
    return false;
}

void ps2_mouse_poll(void) {
    uint32_t n;
    uint64_t flags;

    /* IRQ 12 can be masked or shared with the GPU. Drain AUX bytes here so
     * the desktop pointer still tracks. cli keeps the mouse IRQ from taking
     * the same byte. */
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    for (n = 0; n < 16u; ++n) {
        uint8_t st = inb(PS2_STATUS);
        if ((st & 0x01u) == 0u) {
            break;
        }
        if ((st & 0x20u) == 0u) {
            break;
        }
        input_mouse_irq_byte(inb(PS2_DATA));
    }
    if ((flags & 0x200u) != 0u) {
        __asm__ volatile ("sti");
    }
}

bool keyboard_pop(struct keyboard_event *event) {
    (void)event;
    return false;
}

bool mouse_pop(struct mouse_event *event) {
    (void)event;
    return false;
}
