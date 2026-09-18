#include "ps2.h"
#include "input.h"
#include "irq.h"
#include "port.h"

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
    }
    return false;
}

static bool wait_output_full(void) {
    uint32_t timeout;
    for (timeout = 0; timeout < TIMEOUT; ++timeout) {
        if ((inb(PS2_STATUS) & 0x01u) != 0) {
            return true;
        }
    }
    return false;
}

static bool controller_command(uint8_t command) {
    if (!wait_input_empty()) {
        return false;
    }
    outb(PS2_CMD, command);
    return true;
}

static bool controller_write_data(uint8_t value) {
    if (!wait_input_empty()) {
        return false;
    }
    outb(PS2_DATA, value);
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

bool ps2_init(void) {
    uint8_t config;
    uint8_t result;

    if (!controller_command(0xad) || !controller_command(0xa7)) {
        return false;
    }
    while ((inb(PS2_STATUS) & 0x01u) != 0) {
        (void)inb(PS2_DATA);
    }

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
    if (!controller_command(0xa9) ||
        !controller_read_data(&result) || result != 0x00) {
        return false;
    }

    if (!controller_command(0xae) || !controller_command(0xa8)) {
        return false;
    }
    if (!controller_command(0x60) ||
        !controller_write_data((uint8_t)(config | 0x03u))) {
        return false;
    }

    if (!device_command(false, 0xf4)) {
        return false;
    }
    if (!device_command(true, 0xf6) || !device_command(true, 0xf4)) {
        return false;
    }

    irq_set_handler(1, keyboard_irq);
    irq_set_handler(12, mouse_irq);
    pic_set_mask(1, false);
    pic_set_mask(12, false);
    return true;
}

bool keyboard_pop(struct keyboard_event *event) {
    (void)event;
    return false;
}

bool mouse_pop(struct mouse_event *event) {
    (void)event;
    return false;
}
