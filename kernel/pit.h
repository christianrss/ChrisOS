#ifndef CHRISOS_PIT_H
#define CHRISOS_PIT_H

#include <stdbool.h>
#include <stdint.h>

bool pit_init(uint32_t frequency_hz);
uint64_t pit_ticks(void);

#endif
