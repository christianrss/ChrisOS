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
                if (p[15] >= 2) {
                    uint64_t xsdt = 0;
                    const uint8_t *x;
                    uint32_t len;
                    uint32_t ent;
                    int b;
                    for (b = 0; b < 8; ++b)
                        xsdt |= (uint64_t)p[24 + b] << (8 * b);
                    x = (const uint8_t *)(uintptr_t)bootinfo_phys_to_virt(xsdt);
                    if (x && x[0] == 'X' && x[1] == 'S' && x[2] == 'D' &&
                        x[3] == 'T') {
                        len = (uint32_t)x[4] | ((uint32_t)x[5] << 8) |
                              ((uint32_t)x[6] << 16) | ((uint32_t)x[7] << 24);
                        serial_puts("acpi xsdt\n");
                        for (ent = 36; ent + 8 <= len && ent < 512; ent += 8) {
                            uint64_t tp = 0;
                            const uint8_t *t;
                            char sig[5];
                            for (b = 0; b < 8; ++b)
                                tp |= (uint64_t)x[ent + b] << (8 * b);
                            t = (const uint8_t *)(uintptr_t)
                                bootinfo_phys_to_virt(tp);
                            if (!t)
                                continue;
                            for (b = 0; b < 4; ++b)
                                sig[b] = (char)t[b];
                            sig[4] = 0;
                            if ((sig[0] == 'A' && sig[1] == 'P' &&
                                 sig[2] == 'I' && sig[3] == 'C') ||
                                (sig[0] == 'M' && sig[1] == 'C' &&
                                 sig[2] == 'F' && sig[3] == 'G') ||
                                (sig[0] == 'F' && sig[1] == 'A' &&
                                 sig[2] == 'C' && sig[3] == 'P')) {
                                serial_puts("acpi ");
                                serial_puts(sig);
                                serial_puts("\n");
                            }
                        }
                    }
                }
                return;
            }
        }
    }
    serial_puts("acpi rsdp missing\n");
}
