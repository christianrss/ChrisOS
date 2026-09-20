/* PASSO 07 — bench + fps estimate */
#include "bench.h"
#include "pit.h"

static uint32_t g_last_tick;
static uint32_t g_frames;
static uint32_t g_fps;
static uint32_t g_frame_start;
static uint32_t g_last_frame_ms;
static uint32_t g_clear_start;

void bench_frame_tick(void) {
    uint32_t now = (uint32_t)pit_ticks();
    g_frames++;
    if (g_frame_start == 0)
        g_frame_start = now;
    g_last_frame_ms = now - g_frame_start;
    g_frame_start = now;
    if (now - g_last_tick >= 60) {
        g_fps = g_frames;
        g_frames = 0;
        g_last_tick = now;
    }
}

uint32_t bench_fps_estimate(void) {
    return g_fps;
}

uint32_t bench_frame_ms(void) {
    return g_last_frame_ms;
}

void bench_clear_begin(void) {
    g_clear_start = (uint32_t)pit_ticks();
}

uint32_t bench_clear_end_ms(void) {
    uint32_t now = (uint32_t)pit_ticks();
    if (now < g_clear_start)
        return 0;
    return now - g_clear_start;
}
