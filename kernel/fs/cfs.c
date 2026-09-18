/* LEARN:STOR64-S05 */
#include <stdint.h>
#include "cfs.h"

static void bytes_zero(void *dst, uint32_t n) {
    uint8_t *p = dst;
    uint32_t i;
    for (i = 0; i < n; i++) p[i] = 0u;
}

static void bytes_copy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static int io_error(int rc) {
    return rc == BD_OK ? CFS_OK : CFS_EIO;
}

static int name_length(const char *name, uint32_t *length) {
    uint32_t n = 0;
    if (!name) return CFS_EINVAL;
    while (name[n]) {
        if (name[n] == '/' || name[n] == '\\') return CFS_EINVAL;
        if (n == CFS_NAME_MAX) return CFS_ENAMETOOLONG;
        n++;
    }
    if (!n) return CFS_EINVAL;
    *length = n;
    return CFS_OK;
}

static int name_equal(const CfsDirent *e, const char *name, uint32_t n) {
    uint32_t i;
    if (e->name_len != n) return 0;
    for (i = 0; i < n; i++)
        if (e->name[i] != (uint8_t)name[i]) return 0;
    return 1;
}

static void cache_reset(Cfs *fs) {
    uint32_t i;
    fs->clock = 0u;
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        fs->cache[i].valid = 0u;
        fs->cache[i].lba = 0u;
        fs->cache[i].age = 0u;
    }
}

static CfsCacheLine *cache_victim(Cfs *fs) {
    uint32_t i;
    CfsCacheLine *v = &fs->cache[0];
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        if (!fs->cache[i].valid) return &fs->cache[i];
        if (fs->cache[i].age < v->age) v = &fs->cache[i];
    }
    return v;
}

static int cache_read(Cfs *fs, uint32_t lba, uint8_t out[512]) {
    uint32_t i;
    CfsCacheLine *line;
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        line = &fs->cache[i];
        if (line->valid && line->lba == lba) {
            line->age = ++fs->clock;
            bytes_copy(out, line->data, 512u);
            return CFS_OK;
        }
    }
    line = cache_victim(fs);
    if (bd_read(fs->dev, lba, 1u, line->data) != BD_OK)
        return CFS_EIO;
    line->valid = 1u;
    line->lba = lba;
    line->age = ++fs->clock;
    bytes_copy(out, line->data, 512u);
    return CFS_OK;
}

static int cache_write(Cfs *fs, uint32_t lba, const uint8_t in[512]) {
    uint32_t i;
    if (bd_write(fs->dev, lba, 1u, in) != BD_OK)
        return CFS_EIO;
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        CfsCacheLine *line = &fs->cache[i];
        if (line->valid && line->lba == lba) {
            bytes_copy(line->data, in, 512u);
            line->age = ++fs->clock;
            break;
        }
    }
    return CFS_OK;
}

static int data_lba_valid(uint32_t lba) {
    return lba >= CFS_DATA_LBA &&
           lba < CFS_DATA_LBA + CFS_DATA_SECTORS;
}

static int inode_read(Cfs *fs, uint32_t id, CfsInode *out) {
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    uint32_t lba, off;
    if (id >= CFS_INODE_COUNT) return CFS_ECORRUPT;
    lba = CFS_INODE_LBA + id / per_sector;
    off = (id % per_sector) * CFS_INODE_SIZE;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    if (cfs_inode_decode(out, fs->sector + off) != 0)
        return CFS_ECORRUPT;
    return CFS_OK;
}

static int inode_write(Cfs *fs, uint32_t id, const CfsInode *in) {
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    uint32_t lba, off;
    if (id >= CFS_INODE_COUNT) return CFS_ECORRUPT;
    lba = CFS_INODE_LBA + id / per_sector;
    off = (id % per_sector) * CFS_INODE_SIZE;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    cfs_inode_encode(fs->sector + off, in);
    return cache_write(fs, lba, fs->sector);
}

static int bitmap_set(Cfs *fs, uint32_t index, int used) {
    uint32_t lba, byte, bit;
    if (index >= CFS_DATA_SECTORS) return CFS_ECORRUPT;
    lba = CFS_BITMAP_LBA + index / (STOR_SECTOR_SIZE * 8u);
    byte = (index / 8u) % STOR_SECTOR_SIZE;
    bit = index % 8u;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    if (used)
        fs->sector[byte] |= (uint8_t)(1u << bit);
    else
        fs->sector[byte] &= (uint8_t)~(uint8_t)(1u << bit);
    return cache_write(fs, lba, fs->sector);
}

static int bitmap_get(Cfs *fs, uint32_t index, int *used) {
    uint32_t lba, byte, bit;
    if (index >= CFS_DATA_SECTORS) return CFS_ECORRUPT;
    lba = CFS_BITMAP_LBA + index / (STOR_SECTOR_SIZE * 8u);
    byte = (index / 8u) % STOR_SECTOR_SIZE;
    bit = index % 8u;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    *used = (fs->sector[byte] >> bit) & 1u;
    return CFS_OK;
}

static int block_alloc(Cfs *fs, uint32_t *lba) {
    uint32_t i;
    int used, rc;
    for (i = 0; i < CFS_DATA_SECTORS; i++) {
        rc = bitmap_get(fs, i, &used);
        if (rc != CFS_OK) return rc;
        if (!used) {
            rc = bitmap_set(fs, i, 1);
            if (rc != CFS_OK) return rc;
            bytes_zero(fs->sector, STOR_SECTOR_SIZE);
            rc = cache_write(fs, CFS_DATA_LBA + i, fs->sector);
            if (rc != CFS_OK) {
                (void)bitmap_set(fs, i, 0);
                return rc;
            }
            *lba = CFS_DATA_LBA + i;
            return CFS_OK;
        }
    }
    return CFS_ENOSPC;
}

static int block_free(Cfs *fs, uint32_t lba) {
    if (!data_lba_valid(lba)) return CFS_ECORRUPT;
    return bitmap_set(fs, lba - CFS_DATA_LBA, 0);
}

static int inode_alloc(Cfs *fs, uint32_t *id) {
    uint32_t i;
    CfsInode n;
    int rc;
    for (i = 1u; i < CFS_INODE_COUNT; i++) {
        rc = inode_read(fs, i, &n);
        if (rc != CFS_OK) return rc;
        if (n.type == CFS_INODE_FREE) {
            bytes_zero(&n, (uint32_t)sizeof(n));
            n.type = CFS_INODE_FILE;
            n.generation = fs->super.generation + 1u;
            rc = inode_write(fs, i, &n);
            if (rc != CFS_OK) return rc;
            *id = i;
            return CFS_OK;
        }
    }
    return CFS_ENOSPC;
}

static int dir_find(Cfs *fs, const char *name, uint32_t name_len,
                    uint32_t *inode_id) {
    CfsInode root;
    CfsDirent e;
    uint32_t b, slot;
    int rc = inode_read(fs, CFS_ROOT_INODE, &root);
    if (rc != CFS_OK) return rc;
    if (root.type != CFS_INODE_DIR) return CFS_ECORRUPT;
    for (b = 0; b < CFS_DIRECT_COUNT; b++) {
        if (!root.direct[b]) continue;
        if (!data_lba_valid(root.direct[b])) return CFS_ECORRUPT;
        rc = cache_read(fs, root.direct[b], fs->sector);
        if (rc != CFS_OK) return rc;
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (e.inode && e.name_len && name_equal(&e, name, name_len)) {
                if (e.inode >= CFS_INODE_COUNT) return CFS_ECORRUPT;
                *inode_id = e.inode;
                return CFS_OK;
            }
        }
    }
    return CFS_ENOENT;
}

static int dir_add(Cfs *fs, const char *name, uint32_t name_len,
                   uint32_t inode_id) {
    CfsInode root;
    CfsDirent e;
    uint32_t b, slot, target_b = CFS_DIRECT_COUNT;
    uint32_t target_slot = 0u;
    int rc = inode_read(fs, CFS_ROOT_INODE, &root);
    if (rc != CFS_OK) return rc;

    for (b = 0; b < CFS_DIRECT_COUNT; b++) {
        if (!root.direct[b]) continue;
        rc = cache_read(fs, root.direct[b], fs->sector);
        if (rc != CFS_OK) return rc;
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (!e.inode && target_b == CFS_DIRECT_COUNT) {
                target_b = b;
                target_slot = slot;
            }
        }
    }

    if (target_b == CFS_DIRECT_COUNT) {
        for (b = 0; b < CFS_DIRECT_COUNT; b++)
            if (!root.direct[b]) break;
        if (b == CFS_DIRECT_COUNT) return CFS_ENOSPC;
        rc = block_alloc(fs, &root.direct[b]);
        if (rc != CFS_OK) return rc;
        rc = inode_write(fs, CFS_ROOT_INODE, &root);
        if (rc != CFS_OK) {
            (void)block_free(fs, root.direct[b]);
            return rc;
        }
        target_b = b;
        target_slot = 0u;
    }

    rc = cache_read(fs, root.direct[target_b], fs->sector);
    if (rc != CFS_OK) return rc;
    bytes_zero(&e, (uint32_t)sizeof(e));
    e.inode = inode_id;
    e.type = CFS_INODE_FILE;
    e.name_len = (uint8_t)name_len;
    for (b = 0; b < name_len; b++) e.name[b] = (uint8_t)name[b];
    cfs_dirent_encode(fs->sector + target_slot * CFS_DIRENT_SIZE, &e);
    rc = cache_write(fs, root.direct[target_b], fs->sector);
    if (rc != CFS_OK) return rc;
    root.size += CFS_DIRENT_SIZE;
    return inode_write(fs, CFS_ROOT_INODE, &root);
}

int cfs_format(BlockDevice *dev) {
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsSuper super;
    CfsInode inode;
    uint32_t lba, i, j;
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;

    if (!dev || dev->sector_size != STOR_SECTOR_SIZE ||
        dev->sector_count < STOR_DISK_SECTORS || !dev->writable)
        return CFS_EINVAL;

    bytes_zero(sector, STOR_SECTOR_SIZE);
    for (lba = CFS_SUPER_LBA;
         lba < CFS_DATA_LBA; lba++)
        if (bd_write(dev, lba, 1u, sector) != BD_OK) return CFS_EIO;

    sector[0] = 1u;
    if (bd_write(dev, CFS_BITMAP_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;

    bytes_zero(&inode, (uint32_t)sizeof(inode));
    for (i = 0; i < CFS_INODE_SECTORS; i++) {
        bytes_zero(sector, STOR_SECTOR_SIZE);
        for (j = 0; j < per_sector; j++)
            cfs_inode_encode(sector + j * CFS_INODE_SIZE, &inode);
        if (i == 0u) {
            inode.type = CFS_INODE_DIR;
            inode.generation = 1u;
            inode.direct[0] = CFS_DATA_LBA;
            cfs_inode_encode(sector, &inode);
            bytes_zero(&inode, (uint32_t)sizeof(inode));
        }
        if (bd_write(dev, CFS_INODE_LBA + i, 1u, sector) != BD_OK)
            return CFS_EIO;
    }

    bytes_zero(sector, STOR_SECTOR_SIZE);
    if (bd_write(dev, CFS_DATA_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;

    super.clean = 1u;
    super.generation = 1u;
    cfs_super_encode(sector, &super);
    if (bd_write(dev, CFS_SUPER_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;
    return io_error(bd_flush(dev));
}

int cfs_mount(Cfs *fs, BlockDevice *dev) {
    CfsInode root;
    int rc;
    if (!fs || !dev || dev->sector_size != STOR_SECTOR_SIZE ||
        dev->sector_count < STOR_DISK_SECTORS)
        return CFS_EINVAL;
    bytes_zero(fs, (uint32_t)sizeof(*fs));
    fs->dev = dev;
    if (bd_read(dev, CFS_SUPER_LBA, 1u, fs->sector) != BD_OK)
        return CFS_EIO;
    if (cfs_super_decode(&fs->super, fs->sector) != 0)
        return CFS_EFORMAT;
    cache_reset(fs);
    fs->mounted = 1u;
    rc = inode_read(fs, CFS_ROOT_INODE, &root);
    if (rc != CFS_OK || root.type != CFS_INODE_DIR) {
        fs->mounted = 0u;
        return CFS_ECORRUPT;
    }
    return CFS_OK;
}

int cfs_sync(Cfs *fs) {
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    return io_error(bd_flush(fs->dev));
}

int cfs_create(Cfs *fs, const char *name) {
    uint32_t n, id, existing;
    CfsInode empty;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = name_length(name, &n);
    if (rc != CFS_OK) return rc;
    rc = dir_find(fs, name, n, &existing);
    if (rc == CFS_OK) return CFS_EEXIST;
    if (rc != CFS_ENOENT) return rc;
    rc = inode_alloc(fs, &id);
    if (rc != CFS_OK) return rc;
    rc = dir_add(fs, name, n, id);
    if (rc != CFS_OK) {
        bytes_zero(&empty, (uint32_t)sizeof(empty));
        (void)inode_write(fs, id, &empty);
        return rc;
    }
    fs->super.generation++;
    return (int)id;
}

int cfs_stat(Cfs *fs, const char *name, uint32_t *size) {
    uint32_t n, id;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!size) return CFS_EINVAL;
    rc = name_length(name, &n);
    if (rc != CFS_OK) return rc;
    rc = dir_find(fs, name, n, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (inode.type != CFS_INODE_FILE) return CFS_ECORRUPT;
    *size = inode.size;
    return CFS_OK;
}

int cfs_read(Cfs *fs, const char *name, void *out, uint32_t capacity) {
    uint32_t n, id, done = 0u, amount, block;
    uint8_t *dst = out;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!out && capacity) return CFS_EINVAL;
    rc = name_length(name, &n);
    if (rc != CFS_OK) return rc;
    rc = dir_find(fs, name, n, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (inode.type != CFS_INODE_FILE ||
        inode.size > CFS_MAX_FILE_SIZE) return CFS_ECORRUPT;
    amount = inode.size < capacity ? inode.size : capacity;
    for (block = 0; done < amount; block++) {
        uint32_t take = amount - done;
        if (block >= CFS_DIRECT_COUNT ||
            !data_lba_valid(inode.direct[block])) return CFS_ECORRUPT;
        if (take > STOR_SECTOR_SIZE) take = STOR_SECTOR_SIZE;
        rc = cache_read(fs, inode.direct[block], fs->sector);
        if (rc != CFS_OK) return rc;
        bytes_copy(dst + done, fs->sector, take);
        done += take;
    }
    return (int)done;
}

int cfs_write(Cfs *fs, const char *name, const void *data, uint32_t size) {
    CfsInode old_inode, inode;
    uint32_t n, id, need, old_count, i, added_from, done;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!data && size) return CFS_EINVAL;
    if (size > CFS_MAX_FILE_SIZE) return CFS_EFBIG;
    rc = name_length(name, &n);
    if (rc != CFS_OK) return rc;
    rc = dir_find(fs, name, n, &id);
    if (rc == CFS_ENOENT) {
        rc = cfs_create(fs, name);
        if (rc < 0) return rc;
        id = (uint32_t)rc;
    } else if (rc != CFS_OK) {
        return rc;
    }
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (inode.type != CFS_INODE_FILE ||
        inode.size > CFS_MAX_FILE_SIZE) return CFS_ECORRUPT;
    old_inode = inode;
    old_count = (inode.size + STOR_SECTOR_SIZE - 1u) /
                STOR_SECTOR_SIZE;
    need = (size + STOR_SECTOR_SIZE - 1u) / STOR_SECTOR_SIZE;
    added_from = old_count;

    for (i = old_count; i < need; i++) {
        rc = block_alloc(fs, &inode.direct[i]);
        if (rc != CFS_OK) {
            while (i > added_from) {
                i--;
                (void)block_free(fs, inode.direct[i]);
            }
            return rc;
        }
    }

    done = 0u;
    for (i = 0; i < need; i++) {
        uint32_t take = size - done;
        if (take > STOR_SECTOR_SIZE) take = STOR_SECTOR_SIZE;
        bytes_zero(fs->sector, STOR_SECTOR_SIZE);
        bytes_copy(fs->sector, (const uint8_t *)data + done, take);
        rc = cache_write(fs, inode.direct[i], fs->sector);
        if (rc != CFS_OK) {
            uint32_t j;
            for (j = added_from; j < need; j++)
                (void)block_free(fs, inode.direct[j]);
            return rc;
        }
        done += take;
    }

    inode.size = size;
    inode.generation++;
    for (i = need; i < CFS_DIRECT_COUNT; i++) inode.direct[i] = 0u;
    rc = inode_write(fs, id, &inode);
    if (rc != CFS_OK) {
        for (i = added_from; i < need; i++)
            (void)block_free(fs, inode.direct[i]);
        return rc;
    }
    for (i = need; i < old_count; i++) {
        rc = block_free(fs, old_inode.direct[i]);
        if (rc != CFS_OK) return rc;
    }
    fs->super.generation++;
    return (int)size;
}

int cfs_truncate(Cfs *fs, const char *name, uint32_t size) {
    uint32_t old_size, i;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (size > CFS_MAX_FILE_SIZE) return CFS_EFBIG;
    rc = cfs_stat(fs, name, &old_size);
    if (rc != CFS_OK) return rc;
    rc = cfs_read(fs, name, fs->work, CFS_MAX_FILE_SIZE);
    if (rc < 0) return rc;
    for (i = old_size; i < size; i++) fs->work[i] = 0u;
    return cfs_write(fs, name, fs->work, size);
}

int cfs_list(Cfs *fs, CfsListFn fn, void *ctx) {
    CfsInode root, inode;
    CfsDirent e;
    char name[CFS_NAME_MAX + 1u];
    uint32_t b, slot, i;
    int rc, count = 0;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!fn) return CFS_EINVAL;
    rc = inode_read(fs, CFS_ROOT_INODE, &root);
    if (rc != CFS_OK) return rc;
    for (b = 0; b < CFS_DIRECT_COUNT; b++) {
        if (!root.direct[b]) continue;
        rc = cache_read(fs, root.direct[b], fs->sector);
        if (rc != CFS_OK) return rc;
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (!e.inode || !e.name_len) continue;
            if (e.inode >= CFS_INODE_COUNT ||
                e.name_len > CFS_NAME_MAX) return CFS_ECORRUPT;
            for (i = 0; i < e.name_len; i++) name[i] = (char)e.name[i];
            name[e.name_len] = 0;
            rc = inode_read(fs, e.inode, &inode);
            if (rc != CFS_OK) return rc;
            rc = fn(ctx, name, inode.size, inode.type);
            if (rc) return rc;
            count++;
        }
    }
    return count;
}
