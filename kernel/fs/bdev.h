#ifndef CHRIS_BDEV_H
#define CHRIS_BDEV_H

#include "block_device.h"

#define BD_SLOTS 8

int bd_add(const char *name, const BlockDevice *dev);
int bd_count(void);
BlockDevice *bd_get(int index);
const char *bd_name(int index);
int bd_boot_index(void);
void bd_set_boot(int index);

#endif
