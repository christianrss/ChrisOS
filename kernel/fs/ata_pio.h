/* LEARN:STOR64-S02 */
#ifndef CHRIS_ATA_PIO_H
#define CHRIS_ATA_PIO_H

#include <stdint.h>
#include "block_device.h"

typedef struct AtaPio {
    uint16_t io;
    uint16_t ctrl;
    uint8_t drive;
    uint32_t sectors;
    uint32_t poll_limit;
    uint16_t bm;
    uint8_t dma;
} AtaPio;

void ata_pio_configure(AtaPio *a, uint32_t sectors);
int ata_pio_identify(AtaPio *a, uint32_t *reported_sectors);
void ata_pio_make_device(AtaPio *a, BlockDevice *out);

#endif
