#include <stdint.h>
#include "port.h"

uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(p));
}