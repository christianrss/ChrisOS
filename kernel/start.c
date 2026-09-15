#include <stdint.h>
#include "bootinfo.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "panic.h"
#include "pit.h"
#include "ps2.h"
#include "serial.h"

void kstart(void) {
    const struct bootinfo *boot;
    struct keyboard_event key;
    struct mouse_event mouse;
    uint32_t *pixels;
    uint64_t pitch_pixels;
    uint64_t x;
    uint64_t y;

    if (!serial_init()) {
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    bootinfo_init();
    gdt_init();
    idt_init();
    pic_init();
    if (!pit_init(100)) {
        panic("frequencia PIT invalida");
    }
    if (!ps2_init()) {
        panic("falha ao inicializar PS/2");
    }

    boot = bootinfo_get();
    pixels = (uint32_t *)boot->fb_addr;
    pitch_pixels = boot->fb_pitch / sizeof(uint32_t);
    for (y = 40; y < 80 && y < boot->fb_height; ++y) {
        for (x = 40; x < 200 && x < boot->fb_width; ++x) {
            pixels[y * pitch_pixels + x] = 0x00ffffffu;
        }
    }

    serial_puts("ChrisOS: PS/2 pronto; use teclado e mouse\n");
    for (;;) {
        __asm__ volatile ("sti; hlt");
        while (keyboard_pop(&key)) {
            serial_puts("K code=");
            serial_write_hex(key.scancode);
            serial_puts(key.pressed ? " down" : " up");
            serial_puts(key.extended ? " ext\n" : "\n");
        }
        while (mouse_pop(&mouse)) {
            serial_puts("M dx=");
            serial_write_hex((uint16_t)mouse.dx);
            serial_puts(" dy=");
            serial_write_hex((uint16_t)mouse.dy);
            serial_puts(" buttons=");
            serial_write_hex(mouse.buttons);
            serial_puts("\n");
        }
    }
}
