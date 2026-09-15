#ifndef CHRISOS_PANIC_H
#define CHRISOS_PANIC_H

#include <stdint.h>

_Noreturn void panic(const char *message);
_Noreturn void panic_exception(uint64_t vector, uint64_t error, uint64_t rip);

#endif
