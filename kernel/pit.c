#include <stdint.h>
#include "port.h"

void InitPIT(unsigned int hz) {
    unsigned int div = 1193182u / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)div);
    outb(0x40, (uint8_t)(div >> 8));
}