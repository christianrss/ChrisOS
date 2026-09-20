#include "boot_splash.h"

#include "fs.h"
#include "lang_pipeline.h"
#include "pit.h"
#include "serial.h"

#define BOOT_SPLASH_SRC "GAMES/WATCH.CC"
#define BOOT_SPLASH_CLV "GAMES/WATCH.CLV"

static int g_splash_ready = 0;

int boot_splash_load(void) {
    char probe[1];

    if (fs_read(BOOT_SPLASH_CLV, probe, 1) >= 0) {
        if (!lang_splash_start(BOOT_SPLASH_CLV)) {
            return 0;
        }
        g_splash_ready = 1;
        return 1;
    }
    serial_puts("boot: compiling GAMES/WATCH.CC\n");
    if (!lang_compile_file(BOOT_SPLASH_SRC, BOOT_SPLASH_CLV)) {
        serial_puts("boot: splash compile failed\n");
        return 0;
    }
    if (!lang_splash_start(BOOT_SPLASH_CLV)) {
        serial_puts("boot: splash start failed\n");
        return 0;
    }
    g_splash_ready = 1;
    return 1;
}

void boot_splash_run(int min_frames) {
    uint64_t last_tick = ticks;
    int frames = 0;

    if (!g_splash_ready) {
        return;
    }
    while (frames < min_frames) {
        lang_splash_frame((uint32_t)ticks);
        while (ticks == last_tick) {
            __asm__ volatile ("hlt");
        }
        last_tick = ticks;
        ++frames;
    }
}

void boot_splash_stop(void) {
    if (!g_splash_ready) {
        return;
    }
    lang_splash_stop();
    g_splash_ready = 0;
}
