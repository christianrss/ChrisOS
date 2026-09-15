#include "ps2.h"
#include "irq.h"
#include "port.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64
#define QUEUE_SIZE 64u
#define TIMEOUT    1000000u

static volatile struct keyboard_event keyboard_queue[QUEUE_SIZE];
static volatile uint8_t keyboard_head;
static volatile uint8_t keyboard_tail;
static volatile struct mouse_event mouse_queue[QUEUE_SIZE];
static volatile uint8_t mouse_head;
static volatile uint8_t mouse_tail;
static bool keyboard_extended;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;

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

static void keyboard_push(struct keyboard_event event) {
    uint8_t next = (uint8_t)((keyboard_head + 1u) & (QUEUE_SIZE - 1u));
    if (next == keyboard_tail) {
        return;
    }
    keyboard_queue[keyboard_head] = event;
    keyboard_head = next;
}

static void mouse_push(struct mouse_event event) {
    uint8_t next = (uint8_t)((mouse_head + 1u) & (QUEUE_SIZE - 1u));
    if (next == mouse_tail) {
        return;
    }
    mouse_queue[mouse_head] = event;
    mouse_head = next;
}

static void keyboard_irq(struct irq_frame *frame) {
    uint8_t value;
    struct keyboard_event event;
    (void)frame;

    if ((inb(PS2_STATUS) & 0x01u) == 0) {
        return;
    }
    value = inb(PS2_DATA);
    if (value == 0xe0) {
        keyboard_extended = true;
        return;
    }

    event.scancode = (uint8_t)(value & 0x7fu);
    event.pressed = (value & 0x80u) == 0;
    event.extended = keyboard_extended;
    keyboard_extended = false;
    keyboard_push(event);
}

static void mouse_irq(struct irq_frame *frame) {
    uint8_t value;
    struct mouse_event event;
    (void)frame;

    if ((inb(PS2_STATUS) & 0x21u) != 0x21u) {
        return;
    }
    value = inb(PS2_DATA);
    if (mouse_packet_index == 0 && (value & 0x08u) == 0) {
        return;
    }
    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index != 3) {
        return;
    }
    mouse_packet_index = 0;

    if ((mouse_packet[0] & 0xc0u) != 0) {
        return;
    }
    event.dx = (int16_t)(int8_t)mouse_packet[1];
    event.dy = (int16_t)-(int16_t)(int8_t)mouse_packet[2];
    event.buttons = (uint8_t)(mouse_packet[0] & 0x07u);
    mouse_push(event);
}

bool ps2_init(void) {
    uint8_t config;
    uint8_t result;

    keyboard_head = 0;
    keyboard_tail = 0;
    mouse_head = 0;
    mouse_tail = 0;
    keyboard_extended = false;
    mouse_packet_index = 0;

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
    if (event == 0 || keyboard_tail == keyboard_head) {
        return false;
    }
    *event = keyboard_queue[keyboard_tail];
    keyboard_tail = (uint8_t)((keyboard_tail + 1u) & (QUEUE_SIZE - 1u));
    return true;
}

bool mouse_pop(struct mouse_event *event) {
    if (event == 0 || mouse_tail == mouse_head) {
        return false;
    }
    *event = mouse_queue[mouse_tail];
    mouse_tail = (uint8_t)((mouse_tail + 1u) & (QUEUE_SIZE - 1u));
    return true;
}
