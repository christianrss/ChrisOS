#include "serial.h"
#include "port.h"
#include "spin.h"

#define COM1 0x3f8

static bool serial_available;
static Spinlock g_serial_lock;

bool serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x1e);
    outb(COM1 + 0, 0xae);

    if (inb(COM1 + 0) != 0xae) {
        outb(COM1 + 4, 0x0f);
        serial_available = false;
        return false;
    }

    outb(COM1 + 4, 0x0f);
    serial_available = true;
    spin_init(&g_serial_lock);
    return true;
}

void serial_putc(char value) {
    if (!serial_available) {
        return;
    }
    spin_lock(&g_serial_lock);
    while ((inb(COM1 + 5) & 0x20u) == 0) {
    }
    outb(COM1, (uint8_t)value);
    spin_unlock(&g_serial_lock);
}

void serial_puts(const char *text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text);
        ++text;
    }
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    int shift;

    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> (unsigned)shift) & 0x0fu]);
    }
}

void serial_write_u64(uint64_t value) {
    char buffer[20];
    unsigned int length = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value != 0) {
        buffer[length++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (length != 0) {
        serial_putc(buffer[--length]);
    }
}
