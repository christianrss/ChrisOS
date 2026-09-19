#include "pci.h"
#include "port.h"

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr;

    addr = 0x80000000u;
    addr |= (uint32_t)bus << 16;
    addr |= (uint32_t)slot << 11;
    addr |= (uint32_t)func << 8;
    addr |= (uint32_t)(off & 0xFCu);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t value) {
    uint32_t addr;

    addr = 0x80000000u;
    addr |= (uint32_t)bus << 16;
    addr |= (uint32_t)slot << 11;
    addr |= (uint32_t)func << 8;
    addr |= (uint32_t)(off & 0xFCu);
    outl(0xCF8, addr);
    outl(0xCFC, value);
}

static int virtio_net_match(uint16_t vendor, uint16_t device) {
    if (vendor != 0x1AF4u) {
        return 0;
    }
    return device == 0x1000u || device == 0x1041u;
}

int pci_find_virtio_net(uint16_t *iobase_out) {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t id;
    uint16_t vendor;
    uint16_t device;
    uint32_t bar0;
    uint16_t iobase;
    uint32_t cmd;

    for (bus = 0; bus < 1; ++bus) {
        for (slot = 0; slot < 32; ++slot) {
            for (func = 0; func < 8; ++func) {
                id = pci_read(bus, slot, func, 0x00);
                vendor = (uint16_t)(id & 0xFFFFu);
                device = (uint16_t)((id >> 16) & 0xFFFFu);
                if (vendor == 0xFFFFu) {
                    break;
                }
                if (!virtio_net_match(vendor, device)) {
                    continue;
                }

                cmd = pci_read(bus, slot, func, 0x04);
                cmd |= 0x0005u; /* I/O space + bus master */
                pci_write(bus, slot, func, 0x04, cmd);

                bar0 = pci_read(bus, slot, func, 0x10);
                if ((bar0 & 0x01u) == 0) {
                    continue;
                }
                iobase = (uint16_t)(bar0 & 0xFFFCu);
                if (iobase == 0) {
                    continue;
                }
                *iobase_out = iobase;
                return 1;
            }
        }
    }
    return 0;
}
