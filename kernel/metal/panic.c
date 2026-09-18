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

_Noreturn void panic_exception(uint64_t vector, uint64_t error, uint64_t rip) {
    __asm__ volatile ("cli");
    serial_puts("\nEXCEPTION vector=");
    serial_write_u64(vector);
    serial_puts(" error=");
    serial_write_hex(error);
    serial_puts(" rip=");
    serial_write_hex(rip);
    serial_puts("\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
