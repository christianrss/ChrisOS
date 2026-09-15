#include "desktop.h"
#include "graphics.h"
#include "pit.h"

__attribute__((noreturn)) void desktop_run(void) {
    uint64_t last_tick = ticks;

    for (;;) {
        desktop_frame(ticks);
        gfx_present();

        while (ticks == last_tick) {
            __asm__ volatile ("hlt");
        }
        last_tick = ticks;
    }
}
