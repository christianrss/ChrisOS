/* LEARN:STOR64-S06 */
#ifndef CHRIS_STORAGE_H
#define CHRIS_STORAGE_H

#include "block_device.h"
#include "cfs.h"

int storage_format_if_empty(BlockDevice *dev, int *formatted);
int storage_init(void);
int storage_ready(void);
Cfs *storage_cfs(void);
BlockDevice *storage_disk(void);
int storage_reformat(void);

#endif
