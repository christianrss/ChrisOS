#include "desktop.h"
#include "graphics.h"
#include "lang_pipeline.h"
#include "pit.h"

__attribute__((noreturn)) void desktop_run(void) {
    uint64_t last_tick = ticks;

    for (;;) {
        lang_tick((uint32_t)ticks);
        desktop_frame(ticks);
        gfx_present();

        while (ticks == last_tick) {
            __asm__ volatile ("hlt");
        }
        last_tick = ticks;
    }
}

