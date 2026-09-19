#include "input.h"
#include <stdio.h>

int main(void) {
    InputEvent event;
    InputMouse mouse;
    int n = 0;

    input_init(640, 480);
    input_keyboard_irq(0x1E);
    input_keyboard_irq(0x9E);
    if (!input_next_event(&event)) {
        fprintf(stderr, "FAIL no text event\n");
        return 1;
    }
    if (event.type != INPUT_EVENT_TEXT || event.character != 'a') {
        fprintf(stderr, "FAIL expected 'a'\n");
        return 2;
    }
    if (input_next_event(&event)) {
        fprintf(stderr, "FAIL extra event\n");
        return 3;
    }

    input_keyboard_irq(0xE0);
    input_keyboard_irq(0x48);
    if (!input_next_event(&event) || event.key != INPUT_KEY_UP) {
        fprintf(stderr, "FAIL UP\n");
        return 4;
    }

    input_mouse_irq_byte(0x09);
    input_mouse_irq_byte(0x01);
    input_mouse_irq_byte(0x00);
    mouse = input_mouse_snapshot();
    if (mouse.x != 640 / 2 + 1) {
        fprintf(stderr, "FAIL mouse x=%d\n", mouse.x);
        return 5;
    }
    if (!input_left_pressed()) {
        fprintf(stderr, "FAIL click edge\n");
        return 6;
    }
    input_consume_left_press();
    if (input_left_pressed()) {
        fprintf(stderr, "FAIL click not consumed\n");
        return 7;
    }
    (void)n;
    printf("host input tests passed\n");
    return 0;
}
