#ifndef CHRIS_SPEAKER_H
#define CHRIS_SPEAKER_H

#include <stdint.h>

void speaker_off(void);
void speaker_tone(uint32_t hz);
void speaker_play(uint32_t hz, uint32_t duration_ticks, uint32_t now);
void speaker_poll(uint32_t now);

#endif
