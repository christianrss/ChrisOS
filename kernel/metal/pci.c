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

int pci_find_ide(uint16_t *bm_out) {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t id;
    uint32_t classrev;
    uint32_t bar4;
    uint32_t cmd;
    uint16_t bm;

    for (bus = 0; bus < 1; ++bus) {
        for (slot = 0; slot < 32; ++slot) {
            for (func = 0; func < 8; ++func) {
                id = pci_read(bus, slot, func, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    break;
                }
                classrev = pci_read(bus, slot, func, 0x08);
                if (((classrev >> 16) & 0xFFFFu) != 0x0101u) {
                    continue;
                }
                cmd = pci_read(bus, slot, func, 0x04);
                cmd |= 0x0005u;
                pci_write(bus, slot, func, 0x04, cmd);
                bar4 = pci_read(bus, slot, func, 0x20);
                if ((bar4 & 0x01u) == 0) {
                    continue;
                }
                bm = (uint16_t)(bar4 & 0xFFFCu);
                if (bm == 0) {
                    continue;
                }
                *bm_out = bm;
                return 1;
            }
        }
    }
    return 0;
}

int pci_find_ac97(uint16_t *nam_out, uint16_t *nabm_out, uint8_t *irq_out) {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t id;
    uint32_t classrev;
    uint32_t cmd;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t irq;

    if (!nam_out || !nabm_out || !irq_out) {
        return 0;
    }
    for (bus = 0; bus < 1; ++bus) {
        for (slot = 0; slot < 32; ++slot) {
            for (func = 0; func < 8; ++func) {
                id = pci_read(bus, slot, func, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    break;
                }
                classrev = pci_read(bus, slot, func, 0x08);
                if (((classrev >> 16) & 0xFFFFu) != 0x0401u) {
                    continue;
                }
                cmd = pci_read(bus, slot, func, 0x04);
                cmd |= 0x0005u;
                pci_write(bus, slot, func, 0x04, cmd);
                bar0 = pci_read(bus, slot, func, 0x10);
                bar1 = pci_read(bus, slot, func, 0x14);
                irq = pci_read(bus, slot, func, 0x3C);
                if ((bar0 & 1u) == 0 || (bar1 & 1u) == 0) {
                    continue;
                }
                *nam_out = (uint16_t)(bar0 & 0xFFFCu);
                *nabm_out = (uint16_t)(bar1 & 0xFFFCu);
                *irq_out = (uint8_t)(irq & 0xFFu);
                if (*nam_out == 0 || *nabm_out == 0) {
                    continue;
                }
                return 1;
            }
        }
    }
    return 0;
}
