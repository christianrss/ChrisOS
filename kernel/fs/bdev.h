#ifndef CHRIS_BDEV_H
#define CHRIS_BDEV_H

#include "block_device.h"

#define BD_SLOTS 8

enum {
    BD_OTHER = 0,
    BD_ATA = 1,
    BD_AHCI = 2,
    BD_NVME = 3,
    BD_VIRTIO = 4,
    BD_USB = 5,
    BD_RAM = 6,
    BD_PARTITION = 7
};

enum {
    BD_F_BOOT = 1,
    BD_F_ROOT = 2,
    BD_F_TEST = 4
};

int bd_add(const char *name, const BlockDevice *dev);
int bd_add_kind(const char *name, const BlockDevice *dev, int kind);
int bd_count(void);
BlockDevice *bd_get(int index);
const char *bd_name(int index);
int bd_kind(int index);
int bd_flags(int index);
void bd_set_flags(int index, int flags);
int bd_boot_index(void);
void bd_set_boot(int index);
int bd_installable(int index);

#endif
