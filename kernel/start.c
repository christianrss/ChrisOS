#include <stdint.h>
#include "bootinfo.h"
#include "gdt.h"
#include "graphics.h"
#include "font.h"
#include "heap.h"
#include "idt.h"
#include "input.h"
#include "irq.h"
#include "mm.h"
#include "panic.h"
#include "pit.h"
#include "pmm.h"
#include "ps2.h"
#include "serial.h"

void kstart(void) {
    const struct bootinfo *boot;
    InputEvent event;
    InputMouse mouse;
    uint64_t last_tick;

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
    input_init(g_gfx.width, g_gfx.height);

    serial_puts("ChrisOS: input ponte IRQ pronta\n");
    last_tick = ticks;
    for (;;) {
        gfx_clear(CHRIS_DESKTOP_COLOR);
        gfx_fill_rect(0, 0, g_gfx.width, 40, CHRIS_TASKBAR_COLOR);
        gfx_draw_text(font_row, font_arial_width, font_arial_height,
                      "OK", 8, 8, CHRIS_TEXT_COLOR);
        mouse = input_mouse_snapshot();
        gfx_draw_mouse(mouse.x, mouse.y);
        while (input_next_event(&event)) {
            if (event.type == INPUT_EVENT_TEXT) {
                serial_putc(event.character);
            }
        }
        gfx_present();
        while (ticks == last_tick) {
            __asm__ volatile ("sti; hlt");
        }
        last_tick = ticks;
    }
}
