/* LEARN:WS64-W05 */
#include "cfs.h"
#include "fs_lock.h"
#include "storage_limits.h"

#ifdef __freestanding__
#include "heap.h"
#else
#include <stdlib.h>
#endif

static char g_reason[80];
static uint8_t g_bitmap_fixed[CFS_BITMAP_SECTORS * STOR_SECTOR_SIZE];
static uint8_t g_seen_fixed[CFS_BITMAP_SECTORS * STOR_SECTOR_SIZE];
static uint8_t *g_bitmap;
static uint8_t *g_seen;
static uint8_t *g_bitmap_heap;
static uint8_t *g_seen_heap;
static uint32_t g_data_lba;
static uint32_t g_data_sectors;
static uint32_t g_inode_lba;
static uint32_t g_bitmap_lba;
static uint32_t g_bitmap_sectors;
static uint32_t g_journal_lba;
static uint8_t g_sec[STOR_SECTOR_SIZE];
static uint8_t g_dirsec[STOR_SECTOR_SIZE];
static char g_names[CFS_INODE_COUNT][CFS_NAME_MAX + 1u];
static uint8_t g_anc[CFS_INODE_COUNT];

const char *cfs_fsck_reason(void) {
    return g_reason;
}

static void set_reason(const char *msg) {
    uint32_t i = 0;
    if (g_reason[0]) {
        return;
    }
    while (msg[i] && i < 79u) {
        g_reason[i] = msg[i];
        i++;
    }
    g_reason[i] = 0;
}

static int bit_get(const uint8_t *map, uint32_t index) {
    return (map[index / 8u] >> (index % 8u)) & 1u;
}

static void bit_set(uint8_t *map, uint32_t index) {
    map[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

static void *fsck_alloc(uint32_t size) {
#ifdef __freestanding__
    return kmalloc((uint64_t)size);
#else
    return malloc((size_t)size);
#endif
}

static void fsck_free(void *p) {
    if (!p) return;
#ifdef __freestanding__
    kfree(p);
#else
    free(p);
#endif
}

static void fsck_maps_reset(void) {
    fsck_free(g_bitmap_heap);
    fsck_free(g_seen_heap);
    g_bitmap_heap = 0;
    g_seen_heap = 0;
    g_bitmap = 0;
    g_seen = 0;
}

static void fsck_maps_cleanup(int *held) {
    (void)held;
    fsck_maps_reset();
}

static int data_lba_valid(uint32_t lba) {
    return lba >= g_data_lba &&
           lba < g_data_lba + g_data_sectors;
}

static int note_block(uint32_t lba, int *errors) {
    uint32_t idx;
    if (!data_lba_valid(lba)) {
        set_reason("block lba");
        (*errors)++;
        return -1;
    }
    idx = lba - g_data_lba;
    if (bit_get(g_seen, idx)) {
        set_reason("duplicate block");
        (*errors)++;
        return -1;
    }
    bit_set(g_seen, idx);
    if (!bit_get(g_bitmap, idx)) {
        set_reason("bitmap missing");
        (*errors)++;
        return -1;
    }
    return 0;
}

static int note_ptr_table(BlockDevice *dev, uint32_t lba, int *errors,
                          int depth) {
    uint32_t i, child;
    int rc;
    if (!data_lba_valid(lba)) {
        set_reason("block lba");
        (*errors)++;
        return 0;
    }
    (void)note_block(lba, errors);
    rc = bd_read(dev, lba, 1u, g_sec);
    if (rc != BD_OK) {
        set_reason("indirect io");
        return CFS_EIO;
    }
    if (depth <= 0) return 0;
    for (i = 0; i < CFS_PTRS_PER_BLOCK; i++) {
        child = cfs_get32(g_sec + i * 4u);
        if (!child) continue;
        if (depth == 1)
            (void)note_block(child, errors);
        else {
            uint8_t save[STOR_SECTOR_SIZE];
            uint32_t k;
            for (k = 0; k < STOR_SECTOR_SIZE; k++) save[k] = g_sec[k];
            rc = note_ptr_table(dev, child, errors, depth - 1);
            for (k = 0; k < STOR_SECTOR_SIZE; k++) g_sec[k] = save[k];
            if (rc < 0) return rc;
        }
    }
    return 0;
}

static int name_taken(uint32_t count, const char *name, uint8_t len) {
    uint32_t i, k;
    for (i = 0; i < count; i++) {
        k = 0;
        while (k < len && g_names[i][k] && g_names[i][k] == name[k]) {
            k++;
        }
        if (k == len && g_names[i][k] == 0) {
            return 1;
        }
    }
    return 0;
}

static int walk_dir(BlockDevice *dev, uint32_t id, int *errors);

static int check_dirents(BlockDevice *dev, const CfsInode *inode,
                         uint32_t dir_id, int *errors) {
    CfsDirent ent;
    CfsInode child;
    uint32_t b, slot, k, local = 0u;
    uint32_t per = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    int rc;
    (void)dir_id;
    for (b = 0; b < CFS_DIRECT_COUNT; b++) {
        if (!inode->direct[b] || !data_lba_valid(inode->direct[b])) {
            continue;
        }
        rc = bd_read(dev, inode->direct[b], 1u, g_dirsec);
        if (rc != BD_OK) {
            set_reason("dir io");
            return CFS_EIO;
        }
        local = 0u;
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&ent, g_dirsec + slot * CFS_DIRENT_SIZE);
            if (!ent.inode || !ent.name_len) {
                continue;
            }
            if (ent.inode >= CFS_INODE_COUNT ||
                ent.name_len > CFS_NAME_MAX) {
                set_reason("dirent");
                (*errors)++;
                continue;
            }
            if (name_taken(local, (const char *)ent.name, ent.name_len)) {
                set_reason("name clash");
                (*errors)++;
                continue;
            }
            if (local < CFS_INODE_COUNT) {
                for (k = 0; k < ent.name_len; k++) {
                    g_names[local][k] = (char)ent.name[k];
                }
                g_names[local][ent.name_len] = 0;
                local++;
            }
            rc = bd_read(dev, g_inode_lba + ent.inode / per, 1u, g_sec);
            if (rc != BD_OK) {
                set_reason("inode io");
                return CFS_EIO;
            }
            if (cfs_inode_decode(&child,
                                 g_sec + (ent.inode % per) * CFS_INODE_SIZE) != 0) {
                set_reason("inode checksum");
                (*errors)++;
                continue;
            }
            if (child.type == CFS_INODE_DIR) {
                rc = walk_dir(dev, ent.inode, errors);
                if (rc < 0) {
                    return rc;
                }
            }
        }
    }
    return 0;
}

static int walk_dir(BlockDevice *dev, uint32_t id, int *errors) {
    CfsInode inode;
    uint32_t per = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    int rc;
    if (id >= CFS_INODE_COUNT) {
        set_reason("dirent");
        (*errors)++;
        return 0;
    }
    if (g_anc[id]) {
        set_reason("dir cycle");
        (*errors)++;
        return 0;
    }
    g_anc[id] = 1u;
    rc = bd_read(dev, g_inode_lba + id / per, 1u, g_sec);
    if (rc != BD_OK) {
        set_reason("inode io");
        return CFS_EIO;
    }
    if (cfs_inode_decode(&inode, g_sec + (id % per) * CFS_INODE_SIZE) != 0) {
        set_reason("inode checksum");
        (*errors)++;
        g_anc[id] = 0u;
        return 0;
    }
    if (inode.type != CFS_INODE_DIR) {
        set_reason("root type");
        (*errors)++;
        g_anc[id] = 0u;
        return 0;
    }
    rc = check_dirents(dev, &inode, id, errors);
    g_anc[id] = 0u;
    return rc;
}

int cfs_fsck(Cfs *fs) {
    CFS_LOCK();
    int maps_held __attribute__((cleanup(fsck_maps_cleanup))) = 1;
    CfsSuper super;
    CfsInode inode;
    uint32_t i, b, need, id, per, map_bytes;
    uint64_t max_bytes;
    int errors = 0;
    int rc;

    (void)maps_held;
    g_reason[0] = 0;
    fsck_maps_reset();
    for (i = 0; i < CFS_INODE_COUNT; i++) {
        g_anc[i] = 0u;
    }
    if (!fs || !fs->mounted || !fs->dev) {
        set_reason("not mounted");
        return CFS_ENOTMOUNTED;
    }

    rc = bd_read(fs->dev, CFS_SUPER_LBA, 1u, g_sec);
    if (rc != BD_OK) {
        set_reason("super io");
        return CFS_EIO;
    }
    if (cfs_super_decode(&super, g_sec) != 0) {
        set_reason("super checksum");
        return CFS_ECORRUPT;
    }
    g_data_lba = super.data_lba;
    g_data_sectors = super.data_sectors;
    g_inode_lba = super.inode_lba;
    g_bitmap_lba = super.bitmap_lba;
    g_bitmap_sectors = super.bitmap_sectors;
    g_journal_lba = super.journal_lba;
    if (g_bitmap_sectors == 0u ||
        g_bitmap_sectors > 0xffffffffu / STOR_SECTOR_SIZE) {
        set_reason("bitmap size");
        return CFS_EINVAL;
    }
    map_bytes = g_bitmap_sectors * STOR_SECTOR_SIZE;
    if (g_bitmap_sectors <= CFS_BITMAP_SECTORS) {
        g_bitmap = g_bitmap_fixed;
        g_seen = g_seen_fixed;
    } else {
        g_bitmap_heap = fsck_alloc(map_bytes);
        g_seen_heap = fsck_alloc(map_bytes);
        if (!g_bitmap_heap || !g_seen_heap) {
            set_reason("bitmap size");
            return CFS_ENOSPC;
        }
        g_bitmap = g_bitmap_heap;
        g_seen = g_seen_heap;
    }
    for (i = 0; i < map_bytes; i++) {
        g_bitmap[i] = 0u;
        g_seen[i] = 0u;
    }

    rc = bd_read(fs->dev, g_journal_lba, 1u, g_sec);
    if (rc != BD_OK) {
        set_reason("journal io");
        return CFS_EIO;
    }
    {
        uint32_t magic = cfs_get32(g_sec + 0);
        uint32_t state = cfs_get32(g_sec + 8);
        if (magic && magic != JNL_MAGIC) {
            set_reason("journal magic");
            errors++;
        } else if (magic == JNL_MAGIC && state == JNL_BEGIN) {
            set_reason("journal dirty");
            errors++;
        } else if (magic == JNL_MAGIC && state == JNL_COMMIT) {
            set_reason("journal pending replay");
            errors++;
        }
    }

    for (i = 0; i < g_bitmap_sectors; i++) {
        rc = bd_read(fs->dev, g_bitmap_lba + i, 1u,
                     g_bitmap + i * STOR_SECTOR_SIZE);
        if (rc != BD_OK) {
            set_reason("bitmap io");
            return CFS_EIO;
        }
    }

    per = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    for (id = 0; id < CFS_INODE_COUNT; id++) {
        rc = bd_read(fs->dev, g_inode_lba + id / per, 1u, g_sec);
        if (rc != BD_OK) {
            set_reason("inode io");
            return CFS_EIO;
        }
        if (cfs_inode_decode(&inode,
                             g_sec + (id % per) * CFS_INODE_SIZE) != 0) {
            set_reason("inode checksum");
            errors++;
            continue;
        }
        if (inode.type == CFS_INODE_FREE) {
            continue;
        }
        if (inode.type != CFS_INODE_FILE && inode.type != CFS_INODE_DIR) {
            set_reason("inode type");
            errors++;
            continue;
        }
        max_bytes = (uint64_t)g_data_sectors * STOR_SECTOR_SIZE;
        if (max_bytes > 0xffffffffull)
            max_bytes = 0xffffffffull;
        if ((uint64_t)inode.size > max_bytes) {
            set_reason("size vs blocks");
            errors++;
        }
        if (inode.type == CFS_INODE_FILE) {
            need = (inode.size + STOR_SECTOR_SIZE - 1u) / STOR_SECTOR_SIZE;
            for (b = 0; b < CFS_DIRECT_COUNT; b++) {
                if (b < need) {
                    if (!inode.direct[b]) {
                        set_reason("size vs blocks");
                        errors++;
                    } else {
                        (void)note_block(inode.direct[b], &errors);
                    }
                } else if (inode.direct[b]) {
                    set_reason("size vs blocks");
                    errors++;
                }
            }
        } else {
            for (b = 0; b < CFS_DIRECT_COUNT; b++) {
                if (inode.direct[b]) {
                    (void)note_block(inode.direct[b], &errors);
                }
            }
        }
        if (inode.indirect)
            (void)note_ptr_table(fs->dev, inode.indirect, &errors, 1);
        if (inode.double_indirect)
            (void)note_ptr_table(fs->dev, inode.double_indirect, &errors, 2);
        if (inode.triple_indirect)
            (void)note_ptr_table(fs->dev, inode.triple_indirect, &errors, 3);
        if (id == CFS_ROOT_INODE && inode.type != CFS_INODE_DIR) {
            set_reason("root type");
            errors++;
        }
    }

    rc = walk_dir(fs->dev, CFS_ROOT_INODE, &errors);
    if (rc < 0) {
        return rc;
    }

    for (i = 0; i < g_data_sectors; i++) {
        if (bit_get(g_bitmap, i) && !bit_get(g_seen, i)) {
            set_reason("bitmap leak");
            errors++;
            break;
        }
    }

    if (errors) {
        return errors;
    }
    set_reason("clean");
    return CFS_OK;
}