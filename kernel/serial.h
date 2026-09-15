#ifndef CHRISOS_SERIAL_H
#define CHRISOS_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

bool serial_init(void);
void serial_putc(char value);
void serial_puts(const char *text);
void serial_write_hex(uint64_t value);
void serial_write_u64(uint64_t value);

#endif
