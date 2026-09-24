#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pmm.h"

#define HOST_RAM (48ull * 1024ull * 1024ull)

static uint8_t g_ram[HOST_RAM] __attribute__((aligned(16)));
static __thread uint32_t tls_cpu;
char __kernel_start;
char __kernel_end;
volatile uint32_t cpu_online_count = 1u;

uint32_t smp_current_cpu(void) {
    return tls_cpu;
}

void host_set_cpu(uint32_t id) {
    tls_cpu = id;
}

void panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message ? message : "");
    abort();
}

void serial_puts(const char *text) {
    (void)text;
}

void serial_write_hex(uint64_t value) {
    (void)value;
}

void serial_write_u64(uint64_t value) {
    (void)value;
}

uint64_t bootinfo_phys_to_virt(uint64_t phys) {
    if (phys >= HOST_RAM) {
        fprintf(stderr, "phys_to_virt out of host ram %llu\n",
                (unsigned long long)phys);
        abort();
    }
    return (uint64_t)(uintptr_t)(g_ram + phys);
}

uint64_t bootinfo_memmap_count(void) {
    return 1;
}

int bootinfo_memmap_entry(uint64_t index, uint64_t *base, uint64_t *length,
                          uint64_t *type) {
    if (index != 0 || !base || !length || !type) {
        return -1;
    }
    *base = 0;
    *length = HOST_RAM;
    *type = 0;
    return 0;
}
