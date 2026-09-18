#include "speaker.h"
#include "port.h"

static uint32_t g_stop_tick;
static int g_playing;

void speaker_off(void) {
    uint8_t v = inb(0x61);
    outb(0x61, (uint8_t)(v & (uint8_t)~3u));
    g_playing = 0;
    g_stop_tick = 0;
}

void speaker_tone(uint32_t hz) {
    uint32_t div;
    uint8_t v;

    if (hz == 0u) {
        speaker_off();
        return;
    }
    if (hz < 20u)
        hz = 20u;
    if (hz > 20000u)
        hz = 20000u;
    div = 1193182u / hz;
    if (div == 0u)
        div = 1u;
    if (div > 65535u)
        div = 65535u;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)div);
    outb(0x42, (uint8_t)(div >> 8));
    v = inb(0x61);
    if ((v & 3u) != 3u)
        outb(0x61, (uint8_t)(v | 3u));
}

void speaker_play(uint32_t hz, uint32_t duration_ticks, uint32_t now) {
    if (hz == 0u || duration_ticks == 0u) {
        speaker_off();
        return;
    }
    speaker_tone(hz);
    g_playing = 1;
    g_stop_tick = now + duration_ticks;
}

void speaker_poll(uint32_t now) {
    if (!g_playing)
        return;
    if ((int32_t)(now - g_stop_tick) >= 0)
        speaker_off();
}
