#include "acpi.h"

#include "bootinfo.h"
#include "serial.h"

void acpi_probe(void) {
    uint64_t n = bootinfo_memmap_count();
    uint64_t i;
    for (i = 0; i < n; ++i) {
        uint64_t base = 0;
        uint64_t len = 0;
        uint64_t type = 0;
        uint64_t off;
        if (!bootinfo_memmap_entry(i, &base, &len, &type))
            continue;
        if (base > 0x100000ull || base + len < 0xE0000ull)
            continue;
        for (off = 0xE0000ull; off + 20ull < 0x100000ull; off += 16ull) {
            const uint8_t *p;
            if (off < base || off + 8ull > base + len)
                continue;
            p = (const uint8_t *)(uintptr_t)bootinfo_phys_to_virt(off);
            if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
                p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
                char oem[7];
                int k;
                for (k = 0; k < 6; ++k)
                    oem[k] = (char)p[9 + k];
                oem[6] = 0;
                serial_puts("acpi rsdp oem=");
                serial_puts(oem);
                serial_puts("\n");
                return;
            }
        }
    }
    serial_puts("acpi rsdp missing\n");
}
