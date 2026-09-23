#ifndef CHRIS_AC97_H
#define CHRIS_AC97_H
#include <stdint.h>
int ac97_init(void);
int ac97_write(const int16_t *samples, int n);
int ac97_take_event(int irq);
#endif
