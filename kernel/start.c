#define LIMINE_API_REVISION 3
#include <stdint.h>
#include <limine.h>
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "panic.h"
#include "pit.h"
#include "serial.h"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

void kstart(void) {
    struct limine_framebuffer *framebuffer;
    uint32_t *pixels;
    uint64_t pitch_pixels;
    uint64_t x;
    uint64_t y;
    bool announced = false;

    if (!serial_init()) {
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        panic("Limine base revision 3 nao suportada");
    }
    if (framebuffer_request.response == 0 ||
        framebuffer_request.response->framebuffer_count == 0) {
        panic("framebuffer Limine ausente");
    }

    gdt_init();
    idt_init();
    pic_init();
    if (!pit_init(100)) {
        panic("frequencia PIT invalida");
    }

    framebuffer = framebuffer_request.response->framebuffers[0];
    if (framebuffer == 0 || framebuffer->bpp != 32) {
        panic("framebuffer nao e 32 bpp");
    }
    pixels = (uint32_t *)framebuffer->address;
    pitch_pixels = framebuffer->pitch / sizeof(uint32_t);
    for (y = 40; y < 80 && y < framebuffer->height; ++y) {
        for (x = 40; x < 200 && x < framebuffer->width; ++x) {
            pixels[y * pitch_pixels + x] = 0x00ffffffu;
        }
    }

    serial_puts("ChrisOS: PIC e PIT100 prontos\n");
    for (;;) {
        __asm__ volatile ("sti; hlt");
        if (!announced && pit_ticks() >= 100) {
            announced = true;
            serial_puts("ChrisOS: 100 ticks recebidos\n");
        }
    }
}
