/* PASSO 07 — bench + fps estimate */
#include "bench.h"
#include "pit.h"

static uint32_t g_last_tick;
static uint32_t g_frames;
static uint32_t g_fps;
static uint32_t g_frame_start;
static uint32_t g_last_frame_ms;
static uint32_t g_clear_start;
static uint32_t g_frame_samples[64];
static uint32_t g_frame_sample_n;
static uint32_t g_frame_sample_at;

static uint32_t ticks_to_ms(uint32_t value) {
    return (value * 1000u + 30u) / 60u;
}

void bench_frame_tick(void) {
    uint32_t now = (uint32_t)pit_ticks();
    g_frames++;
    if (g_frame_start == 0)
        g_frame_start = now;
    g_last_frame_ms = ticks_to_ms(now - g_frame_start);
    g_frame_samples[g_frame_sample_at] = g_last_frame_ms;
    g_frame_sample_at = (g_frame_sample_at + 1u) % 64u;
    if (g_frame_sample_n < 64u)
        g_frame_sample_n++;
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

static uint32_t frame_percentile(uint32_t percentile) {
    uint32_t copy[64];
    uint32_t i;
    uint32_t j;
    uint32_t at;
    if (g_frame_sample_n == 0u)
        return 0u;
    for (i = 0; i < g_frame_sample_n; ++i)
        copy[i] = g_frame_samples[i];
    for (i = 0; i < g_frame_sample_n; ++i) {
        uint32_t best = i;
        for (j = i + 1u; j < g_frame_sample_n; ++j) {
            if (copy[j] < copy[best])
                best = j;
        }
        if (best != i) {
            uint32_t tmp = copy[i];
            copy[i] = copy[best];
            copy[best] = tmp;
        }
    }
    at = ((g_frame_sample_n - 1u) * percentile) / 100u;
    return copy[at];
}

uint32_t bench_frame_p50_ms(void) {
    return frame_percentile(50u);
}

uint32_t bench_frame_p95_ms(void) {
    return frame_percentile(95u);
}

void bench_clear_begin(void) {
    g_clear_start = (uint32_t)pit_ticks();
}

uint32_t bench_clear_end_ms(void) {
    uint32_t now = (uint32_t)pit_ticks();
    if (now < g_clear_start)
        return 0;
    return ticks_to_ms(now - g_clear_start);
}
