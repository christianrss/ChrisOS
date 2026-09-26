#include "input.h"
#include <stdio.h>

static int fail(const char *msg) {
    fprintf(stderr, "fail: %s\n", msg);
    return 1;
}

int main(void) {
    int dx;
    int dy;
    input_init(800, 600);
    input_keystate_clear();

    input_keystate_note(0x4Bu);
    if (!input_key_down(0x4B))
        return fail("keypad 4 down");
    input_keystate_note(0xCBu);
    if (input_key_down(0x4B))
        return fail("keypad 4 up");

    input_keystate_note(0xE0u);
    input_keystate_note(0x4Bu);
    if (input_key_down(0x4B))
        return fail("arrow left collided with keypad 4");
    if (!input_key_down(INPUT_SCAN_LEFT))
        return fail("arrow left");
    input_keystate_note(0xE0u);
    input_keystate_note(0xCBu);
    if (input_key_down(INPUT_SCAN_LEFT))
        return fail("arrow left release");

    input_keystate_note(0xE0u);
    input_keystate_note(0x48u);
    input_keystate_note(0x4Bu);
    if (!input_key_down(INPUT_SCAN_UP))
        return fail("arrow up still down");
    if (input_key_down(0x4B) == 0 && input_key_down(INPUT_SCAN_LEFT))
        return fail("plain 4B became left arrow");
    if (!input_key_down(0x4B))
        return fail("keypad 4 after arrow prefix consumed");

    input_keyboard_irq(0x2Au);
    input_keyboard_irq(0x1Eu);
    input_keyboard_irq(0x1Eu);
    if (!input_key_down(0x2A) || !input_key_down(0x1E))
        return fail("shift make");
    input_keyboard_irq(0x9Eu);
    if (!input_key_down(0x2A) || input_key_down(0x1E))
        return fail("shift held across A release");
    input_keyboard_irq(0xAAu);
    if (input_key_down(0x2A))
        return fail("shift break");

    input_keyboard_irq(0x1Du);
    input_keyboard_irq(0x38u);
    if (!input_key_down(0x1D) || !input_key_down(0x38))
        return fail("ctrl alt");
    input_keystate_note(0xE0u);
    input_keyboard_irq(0x38u);
    if (!input_key_down(INPUT_SCAN_EXT + 0x38) || !input_key_down(0x38))
        return fail("altgr distinct from alt");
    input_keyboard_irq(0xB8u);
    input_keystate_note(0xE0u);
    input_keyboard_irq(0xB8u);
    input_keyboard_irq(0x9Du);
    if (input_key_down(0x1D) || input_key_down(0x38) ||
        input_key_down(INPUT_SCAN_EXT + 0x38))
        return fail("ctrl alt release");

    input_set_layout(INPUT_LAYOUT_ABNT2);
    if (input_get_layout() != INPUT_LAYOUT_ABNT2)
        return fail("layout");
    input_set_layout(INPUT_LAYOUT_US);

    input_init(800, 600);
    input_mouse_irq_byte(0x08);
    input_mouse_irq_byte(0x03);
    input_mouse_irq_byte(0xFEu);
    input_capture_set(7);
    input_mouse_irq_byte(0x08);
    input_mouse_irq_byte(0x04);
    input_mouse_irq_byte(0x02);
    if (!input_mouse_delta_for(7, &dx, &dy))
        return fail("capture delta");
    if (dx != 4 || dy != -2)
        return fail("ps2 delta sign");
    if (input_mouse_delta_for(3, &dx, &dy))
        return fail("other task stole delta");
    input_keyboard_irq(0x01u);
    if (input_capture_owner() != -1)
        return fail("esc release");

    input_use_absolute(1);
    input_capture_set(9);
    input_pointer_absolute(10, 10, 100, 100, 0);
    input_mouse_delta(&dx, &dy);
    input_pointer_absolute(40, 70, 100, 100, 0);
    dx = input_mouse_axis(9, 0);
    dy = input_mouse_axis(9, 1);
    if (dx <= 0 || dy <= 0)
        return fail("absolute capture delta");
    if (input_mouse_axis(4, 0) != 0)
        return fail("uncaptured task saw a delta");
    puts("test_keystate: ok");
    return 0;
}
