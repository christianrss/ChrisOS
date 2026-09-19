#ifndef CHRISOS_PCI_H
#define CHRISOS_PCI_H

#include <stdint.h>

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t value);

/* Returns 1 and fills iobase when virtio-net PCI (legacy IO BAR) is found. */
int pci_find_virtio_net(uint16_t *iobase_out);

#endif
