#ifndef CHRIS_BENCH_H
#define CHRIS_BENCH_H

#include <stdint.h>

void bench_frame_tick(void);
uint32_t bench_fps_estimate(void);
uint32_t bench_frame_ms(void);
void bench_clear_begin(void);
uint32_t bench_clear_end_ms(void);

#endif
