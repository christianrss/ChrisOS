#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static uint8_t keys[128];

static void note(uint8_t scancode) {
    uint8_t code;
    if (scancode == 0xE0u)
        return;
    code = (uint8_t)(scancode & 0x7Fu);
    keys[code] = (scancode & 0x80u) ? 0 : 1;
}

static int down(int scancode) {
    if (scancode < 0 || scancode > 127)
        return 0;
    return keys[scancode] ? 1 : 0;
}

int main(void) {
    note(0x4Bu);
    assert(down(0x4B) == 1);
    note(0xCBu);
    assert(down(0x4B) == 0);
    note(0xE0u);
    note(0x4Du);
    assert(down(0x4D) == 1);
    note(0xE0u);
    note(0xCDu);
    assert(down(0x4D) == 0);
    assert(down(200) == 0);
    puts("test_keystate: ok");
    return 0;
}
