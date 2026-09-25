#include "panic.h"
#include "buildid.h"
#include "serial.h"
#include "smp.h"

static void panic_identity(void) {
    uint64_t cr3;
    uint64_t rsp;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    serial_puts(" cpu=");
    serial_write_u64(smp_current_cpu());
    serial_puts(" cr3=");
    serial_write_hex(cr3);
    serial_puts(" rsp=");
    serial_write_hex(rsp);
    serial_puts(" build=");
    serial_puts(build_id());
    serial_puts(" git=");
    serial_puts(build_git());
    serial_puts(" sha256=");
    serial_puts(build_kernel_sha256());
    serial_puts("\n");
}

_Noreturn void panic(const char *message) {
    __asm__ volatile ("cli");
    serial_puts("\nPANIC: ");
    serial_puts(message);
    panic_identity();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

_Noreturn void panic_exception(uint64_t vector, uint64_t error, uint64_t rip) {
    uint64_t cr2;
    __asm__ volatile ("cli");
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    serial_puts("\nEXCEPTION vector=");
    serial_write_u64(vector);
    serial_puts(" error=");
    serial_write_hex(error);
    serial_puts(" rip=");
    serial_write_hex(rip);
    serial_puts(" cr2=");
    serial_write_hex(cr2);
    panic_identity();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
