#include <stdint.h>
#include "bootinfo.h"
#include "gdt.h"
#include "graphics.h"
#include "heap.h"
#include "idt.h"
#include "irq.h"
#include "mm.h"
#include "panic.h"
#include "pit.h"
#include "pmm.h"
#include "ps2.h"
#include "serial.h"

void kstart(void) {
    const struct bootinfo *boot;
    struct keyboard_event key;
    struct mouse_event mouse;

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

    pmm_init();
    pmm_selftest();
    mm_init();
    mm_selftest();
    heap_init();
    heap_selftest();

    boot = bootinfo_get();
    if (boot->fb_bpp != 32 ||
        !gfx_init((uint32_t *)(uintptr_t)boot->fb_addr,
                  (int)boot->fb_width,
                  (int)boot->fb_height,
                  (int)boot->fb_pitch)) {
        panic("gfx_init recusou o framebuffer");
    }

    gfx_clear(CHRIS_DESKTOP_COLOR);
    gfx_fill_rect(0, 0, g_gfx.width, 40, CHRIS_TASKBAR_COLOR);
    gfx_present();
    serial_puts("ChrisOS: gfx 32bpp pitch ok\n");

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
