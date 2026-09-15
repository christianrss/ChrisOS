#include "panic.h"
#include "serial.h"

_Noreturn void panic(const char *message) {
    __asm__ volatile ("cli");
    serial_puts("\nPANIC: ");
    serial_puts(message);
    serial_puts("\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
