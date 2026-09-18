/* LEARN:STOR64-S03 */
#include <stdint.h>
#include "storage_limits.h"
#include "disk_smoke.h"

int disk_smoke_test(BlockDevice *dev) {
    static uint8_t sent[STOR_SECTOR_SIZE];
    static uint8_t got[STOR_SECTOR_SIZE];
    uint32_t i;
    int rc;

    for (i = 0; i < STOR_SECTOR_SIZE; i++) {
        sent[i] = (uint8_t)(0x5au ^ (uint8_t)i);
        got[i] = 0u;
    }
    sent[0] = 'C';
    sent[1] = 'H';
    sent[2] = 'R';
    sent[3] = 'D';

    rc = bd_write(dev, STOR_DISK_SECTORS - 1u, 1u, sent);
    if (rc != BD_OK)
        return rc;
    rc = bd_flush(dev);
    if (rc != BD_OK)
        return rc;
    rc = bd_read(dev, STOR_DISK_SECTORS - 1u, 1u, got);
    if (rc != BD_OK)
        return rc;
    for (i = 0; i < STOR_SECTOR_SIZE; i++)
        if (sent[i] != got[i])
            return BD_EIO;
    return BD_OK;
}
