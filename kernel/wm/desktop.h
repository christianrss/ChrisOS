#ifndef CHRIS_DESKTOP_H
#define CHRIS_DESKTOP_H

#include <stdint.h>

void desktop_init(void);
void desktop_boot_apps(void);
void desktop_frame(uint64_t ticks);
__attribute__((noreturn)) void desktop_run(void);

#endif
