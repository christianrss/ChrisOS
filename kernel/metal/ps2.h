#ifndef CHRISOS_PS2_H
#define CHRISOS_PS2_H

#include <stdbool.h>
#include <stdint.h>

struct keyboard_event {
    uint8_t scancode;
    bool pressed;
    bool extended;
};

struct mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
};

bool ps2_init(void);
void ps2_mouse_poll(void);
bool keyboard_pop(struct keyboard_event *event);
bool mouse_pop(struct mouse_event *event);

#endif
