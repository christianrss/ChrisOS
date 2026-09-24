#include "desktop.h"
#include "graphics.h"
#include "lang_pipeline.h"
#include "pit.h"
#include "clvm_sys.h"
#include "mm.h"
#include "net.h"

__attribute__((noreturn)) void desktop_run(void) {
    uint64_t last_tick = ticks;

    for (;;) {
        net_poll();
        clvm_sys_frame((uint32_t)ticks);
        lang_tick((uint32_t)ticks);
        desktop_frame(ticks);
        gfx_present();

        while (ticks == last_tick) {
            mm_tlb_poll();
            __asm__ volatile ("hlt");
        }
        mm_tlb_poll();
        last_tick = ticks;
    }
}

