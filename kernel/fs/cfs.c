/* LEARN:WS64-W05 */
#include <stdint.h>
#include "cfs.h"

#ifdef __freestanding__
#include "heap.h"
#include "serial.h"
#else
#include <stdlib.h>
#endif

static void *cfs_work_alloc(uint32_t size) {
#ifdef __freestanding__
    return kmalloc((uint64_t)size);
#else
    return malloc((size_t)size);
#endif
}

static void cfs_work_free(void *p) {
    if (!p) return;
#ifdef __freestanding__
    kfree(p);
#else
    free(p);
#endif
}

#define CFS_COMP_MAX CFS_NAME_MAX
#define CFS_DIR_MAX_BLOCKS (CFS_DIRECT_COUNT + CFS_PTRS_PER_BLOCK)

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

static uint32_t inode_effective_mode(const CfsInode *n) {
    if (n->mode != 0u)
        return n->mode;
    if (((uint32_t)n->flags & CFS_PERM_ALL) != 0u)
        return (uint32_t)n->flags & CFS_PERM_ALL;
    if (n->type != CFS_INODE_FREE)
        return CFS_PERM_ALL;
    return 0u;
}

static int cfs_perm_need(const CfsInode *n, uint32_t bit) {
    if (n->uid != 0u)
        return CFS_EPERM;
    if ((inode_effective_mode(n) & bit) == 0u)
        return CFS_EPERM;
    return CFS_OK;
}

static void inode_init_acl(CfsInode *n) {
    n->uid = 0u;
    n->gid = 0u;
    n->mode = CFS_PERM_ALL;
    n->flags = (uint16_t)CFS_PERM_ALL;
}

static int name_equal(const CfsDirent *e, const char *name, uint32_t n) {
    uint32_t i;
    if (e->name_len != n) return 0;
    for (i = 0; i < n; i++)
        if (e->name[i] != (uint8_t)name[i]) return 0;
    return 1;
}

static int parse_path(const char *path, PathParts *p) {
    uint32_t i = 0;
    uint32_t n = 0;
    uint32_t total = 0;
    if (!path || !p) return CFS_EINVAL;
    bytes_zero(p, (uint32_t)sizeof(*p));
    while (path[total]) {
        if (total >= CFS_PATH_MAX) return CFS_ENAMETOOLONG;
        total++;
    }
    p->total = total;
    p->s = path;
    if (path[0] == '/') {
        i = 1;
        p->s = path + 1;
        if (!path[1]) {
            p->ncomp = 0;
            return CFS_OK;
        }
    }
    if (!p->s[0]) {
        p->ncomp = 0;
        return CFS_OK;
    }
    i = 0;
    while (p->s[i]) {
        uint32_t st;
        uint32_t ln;
        if (n >= CFS_PATH_DEPTH) return CFS_EINVAL;
        if (p->s[i] == '/') return CFS_EINVAL;
        st = i;
        ln = 0;
        while (p->s[i] && p->s[i] != '/') {
            if (p->s[i] == '\\') return CFS_EINVAL;
            ln++;
            if (ln > CFS_COMP_MAX) return CFS_ENAMETOOLONG;
            i++;
        }
        if (ln == 1u && p->s[st] == '.') return CFS_EINVAL;
        if (ln == 2u && p->s[st] == '.' && p->s[st + 1] == '.') return CFS_EINVAL;
        p->start[n] = st;
        p->len[n] = ln;
        n++;
        if (p->s[i] == '/') {
            i++;
            if (!p->s[i]) return CFS_EINVAL;
        }
    }
    p->ncomp = n;
    return CFS_OK;
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

static void cache_drop_lba(Cfs *fs, uint32_t lba) {
    uint32_t i;
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        if (fs->cache[i].valid && fs->cache[i].lba == lba) {
            fs->cache[i].valid = 0u;
        }
    }
}

static int cache_read(Cfs *fs, uint32_t lba, uint8_t out[512]) {
    uint32_t i;
    CfsCacheLine *line = 0;
    CfsCacheLine *best = 0;
    for (i = 0; i < CFS_CACHE_LINES; i++) {
        line = &fs->cache[i];
        if (line->valid && line->lba == lba) {
            if (!best || line->age > best->age) {
                best = line;
            }
        }
    }
    if (best) {
        for (i = 0; i < CFS_CACHE_LINES; i++) {
            line = &fs->cache[i];
            if (line != best && line->valid && line->lba == lba) {
                line->valid = 0u;
            }
        }
        best->age = ++fs->clock;
        bytes_copy(out, best->data, 512u);
        return CFS_OK;
    }
    cache_drop_lba(fs, lba);
    line = cache_victim(fs);
    if (bd_read(fs->dev, lba, 1u, line->data) != BD_OK)
        return CFS_EIO;
    line->valid = 1u;
    line->lba = lba;
    line->age = ++fs->clock;
    bytes_copy(out, line->data, 512u);
    return CFS_OK;
}

static int cache_write_raw(Cfs *fs, uint32_t lba, const uint8_t in[512]) {
    CfsCacheLine *line;
    if (bd_write(fs->dev, lba, 1u, in) != BD_OK)
        return CFS_EIO;
    cache_drop_lba(fs, lba);
    line = cache_victim(fs);
    line->valid = 1u;
    line->lba = lba;
    bytes_copy(line->data, in, 512u);
    line->age = ++fs->clock;
    return CFS_OK;
}

static int cache_write(Cfs *fs, uint32_t lba, const uint8_t in[512]) {
    if (fs->jnl_active && !fs->jnl_data) {
        int rc = jnl_log(&fs->jnl, lba, in);
        if (rc != CFS_OK) return rc;
    }
    return cache_write_raw(fs, lba, in);
}

static int jnl_write_hdr(Cfs *fs, uint32_t state, uint32_t seq, uint32_t nrec) {
    uint8_t s[STOR_SECTOR_SIZE];
    cfs_zero(s, STOR_SECTOR_SIZE);
    cfs_put32(s + 0, JNL_MAGIC);
    cfs_put32(s + 4, seq);
    cfs_put32(s + 8, state);
    cfs_put32(s + 12, nrec);
    cfs_put32(s + 16, cfs_checksum(s, 16u));
    return cache_write_raw(fs, CFS_JOURNAL_LBA, s);
}

int jnl_begin(Jnl *j, Cfs *fs) {
    j->fs = fs;
    j->seq = fs->super.generation;
    j->nrec = 0u;
    return jnl_write_hdr(fs, JNL_BEGIN, j->seq, 0u);
}

int jnl_log(Jnl *j, uint32_t lba, const uint8_t data[512]) {
    uint8_t s[STOR_SECTOR_SIZE];
    uint32_t slot;
    if (j->nrec >= JNL_MAX_REC) return CFS_ENOSPC;
    slot = CFS_JOURNAL_LBA + 1u + j->nrec * 2u;
    cfs_zero(s, STOR_SECTOR_SIZE);
    cfs_put32(s + 0, lba);
    cfs_put32(s + 4, cfs_checksum(data, 512u));
    if (cache_write_raw(j->fs, slot, s) != CFS_OK) return CFS_EIO;
    if (cache_write_raw(j->fs, slot + 1u, data) != CFS_OK) return CFS_EIO;
    j->rec_lba[j->nrec++] = lba;
    return CFS_OK;
}

int jnl_commit(Jnl *j) {
    int rc = jnl_write_hdr(j->fs, JNL_COMMIT, j->seq, j->nrec);
    if (rc != CFS_OK) return rc;
    return jnl_write_hdr(j->fs, JNL_EMPTY, j->seq + 1u, 0u);
}

int jnl_replay(Cfs *fs, uint32_t *replayed) {
    uint8_t hdr[STOR_SECTOR_SIZE];
    uint8_t meta[STOR_SECTOR_SIZE];
    uint8_t data[STOR_SECTOR_SIZE];
    uint32_t magic, state, nrec, i, slot, lba, sum;
    if (replayed) *replayed = 0u;
    if (cache_read(fs, CFS_JOURNAL_LBA, hdr) != CFS_OK) return CFS_EIO;
    magic = cfs_get32(hdr + 0);
    if (!magic) return CFS_OK;
    if (magic != JNL_MAGIC) return CFS_ECORRUPT;
    if (cfs_get32(hdr + 16) != cfs_checksum(hdr, 16u)) return CFS_ECORRUPT;
    state = cfs_get32(hdr + 8);
    nrec = cfs_get32(hdr + 12);
    if (state == JNL_EMPTY) return CFS_OK;
    if (state == JNL_BEGIN) {
        cfs_zero(hdr, STOR_SECTOR_SIZE);
        return cache_write_raw(fs, CFS_JOURNAL_LBA, hdr);
    }
    if (state != JNL_COMMIT) return CFS_ECORRUPT;
    for (i = 0; i < nrec && i < JNL_MAX_REC; i++) {
        slot = CFS_JOURNAL_LBA + 1u + i * 2u;
        if (cache_read(fs, slot, meta) != CFS_OK) return CFS_EIO;
        if (cache_read(fs, slot + 1u, data) != CFS_OK) return CFS_EIO;
        lba = cfs_get32(meta + 0);
        sum = cfs_get32(meta + 4);
        if (sum != cfs_checksum(data, 512u)) return CFS_ECORRUPT;
        if (cache_write_raw(fs, lba, data) != CFS_OK) return CFS_EIO;
        if (replayed) (*replayed)++;
    }
    cfs_zero(hdr, STOR_SECTOR_SIZE);
    cfs_put32(hdr + 0, JNL_MAGIC);
    cfs_put32(hdr + 8, JNL_EMPTY);
    cfs_put32(hdr + 16, cfs_checksum(hdr, 16u));
    return cache_write_raw(fs, CFS_JOURNAL_LBA, hdr);
}

static void jnl_serial_mount(uint32_t state, uint32_t replayed) {
#ifdef __freestanding__
    if (state == JNL_EMPTY || !state) {
        serial_puts("journal: empty\n");
    } else if (state == JNL_BEGIN) {
        serial_puts("journal: dropped (no commit)\n");
    } else if (state == JNL_COMMIT) {
        serial_puts("journal: replayed ");
        serial_write_u64((uint64_t)replayed);
        serial_puts(" records\n");
    }
#else
    (void)state;
    (void)replayed;
#endif
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

static int ptr_block_get(Cfs *fs, uint32_t lba, uint32_t index, uint32_t *out) {
    int rc;
    if (index >= CFS_PTRS_PER_BLOCK) return CFS_ECORRUPT;
    rc = cache_read(fs, lba, fs->sector);
    if (rc != CFS_OK) return rc;
    *out = cfs_get32(fs->sector + index * 4u);
    return CFS_OK;
}

static int ptr_block_set(Cfs *fs, uint32_t lba, uint32_t index, uint32_t val) {
    int rc;
    if (index >= CFS_PTRS_PER_BLOCK) return CFS_ECORRUPT;
    rc = cache_read(fs, lba, fs->sector);
    if (rc != CFS_OK) return rc;
    cfs_put32(fs->sector + index * 4u, val);
    return cache_write(fs, lba, fs->sector);
}

static int file_lba(Cfs *fs, CfsInode *n, uint32_t block, uint32_t *lba,
                    int alloc) {
    uint32_t idx, mid, leaf, t;
    int rc;

    if (block >= CFS_MAX_BLOCKS) {
        return alloc ? CFS_ENOSPC : CFS_ECORRUPT;
    }
    if (block < CFS_DIRECT_COUNT) {
        if (!n->direct[block]) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &n->direct[block]);
            if (rc != CFS_OK) return rc;
        }
        *lba = n->direct[block];
        return CFS_OK;
    }
    idx = block - CFS_DIRECT_COUNT;
    if (idx < CFS_PTRS_PER_BLOCK) {
        if (!n->indirect) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &n->indirect);
            if (rc != CFS_OK) return rc;
        }
        rc = ptr_block_get(fs, n->indirect, idx, &t);
        if (rc != CFS_OK) return rc;
        if (!t) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &t);
            if (rc != CFS_OK) return rc;
            rc = ptr_block_set(fs, n->indirect, idx, t);
            if (rc != CFS_OK) return rc;
        }
        *lba = t;
        return CFS_OK;
    }
    idx -= CFS_PTRS_PER_BLOCK;
    mid = idx / CFS_PTRS_PER_BLOCK;
    leaf = idx % CFS_PTRS_PER_BLOCK;
    if (!n->double_indirect) {
        if (!alloc) return CFS_ECORRUPT;
        rc = block_alloc(fs, &n->double_indirect);
        if (rc != CFS_OK) return rc;
    }
    rc = ptr_block_get(fs, n->double_indirect, mid, &t);
    if (rc != CFS_OK) return rc;
    if (!t) {
        if (!alloc) return CFS_ECORRUPT;
        rc = block_alloc(fs, &t);
        if (rc != CFS_OK) return rc;
        rc = ptr_block_set(fs, n->double_indirect, mid, t);
        if (rc != CFS_OK) return rc;
    }
    {
        uint32_t data = 0;
        rc = ptr_block_get(fs, t, leaf, &data);
        if (rc != CFS_OK) return rc;
        if (!data) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &data);
            if (rc != CFS_OK) return rc;
            rc = ptr_block_set(fs, t, leaf, data);
            if (rc != CFS_OK) return rc;
        }
        *lba = data;
        return CFS_OK;
    }
}

static int inode_ptr_free(Cfs *fs, CfsInode *n) {
    uint32_t k, mid;
    int rc;
    if (n->indirect) {
        rc = block_free(fs, n->indirect);
        if (rc != CFS_OK) return rc;
        n->indirect = 0u;
    }
    if (n->double_indirect) {
        rc = cache_read(fs, n->double_indirect, fs->sector);
        if (rc != CFS_OK) return rc;
        for (k = 0; k < CFS_PTRS_PER_BLOCK; k++) {
            mid = cfs_get32(fs->sector + k * 4u);
            if (mid) {
                rc = block_free(fs, mid);
                if (rc != CFS_OK) return rc;
            }
        }
        rc = block_free(fs, n->double_indirect);
        if (rc != CFS_OK) return rc;
        n->double_indirect = 0u;
    }
    return CFS_OK;
}

static int inode_ptr_prune(Cfs *fs, CfsInode *n, uint32_t need) {
    uint32_t i, idx, mid_idx, leaf_idx, mid, k;
    int rc;

    for (i = need; i < CFS_DIRECT_COUNT; i++)
        n->direct[i] = 0u;
    if (need <= CFS_DIRECT_COUNT) {
        return inode_ptr_free(fs, n);
    }
    idx = need - CFS_DIRECT_COUNT;
    if (need <= CFS_DIRECT_COUNT + CFS_PTRS_PER_BLOCK) {
        if (n->indirect) {
            for (i = idx; i < CFS_PTRS_PER_BLOCK; i++) {
                rc = ptr_block_set(fs, n->indirect, i, 0u);
                if (rc != CFS_OK) return rc;
            }
        }
        if (n->double_indirect) {
            rc = cache_read(fs, n->double_indirect, fs->sector);
            if (rc != CFS_OK) return rc;
            for (k = 0; k < CFS_PTRS_PER_BLOCK; k++) {
                mid = cfs_get32(fs->sector + k * 4u);
                if (mid) {
                    rc = block_free(fs, mid);
                    if (rc != CFS_OK) return rc;
                }
            }
            rc = block_free(fs, n->double_indirect);
            if (rc != CFS_OK) return rc;
            n->double_indirect = 0u;
        }
        return CFS_OK;
    }
    idx = need - CFS_DIRECT_COUNT - CFS_PTRS_PER_BLOCK;
    if (!n->double_indirect) {
        return CFS_OK;
    }
    if (idx >= CFS_PTRS_PER_BLOCK * CFS_PTRS_PER_BLOCK) {
        return CFS_OK;
    }
    mid_idx = idx / CFS_PTRS_PER_BLOCK;
    leaf_idx = idx % CFS_PTRS_PER_BLOCK;
    rc = cache_read(fs, n->double_indirect, fs->sector);
    if (rc != CFS_OK) return rc;
    for (i = mid_idx + 1u; i < CFS_PTRS_PER_BLOCK; i++) {
        mid = cfs_get32(fs->sector + i * 4u);
        if (mid) {
            rc = block_free(fs, mid);
            if (rc != CFS_OK) return rc;
            cfs_put32(fs->sector + i * 4u, 0u);
        }
    }
    mid = cfs_get32(fs->sector + mid_idx * 4u);
    if (mid) {
        for (k = leaf_idx; k < CFS_PTRS_PER_BLOCK; k++) {
            uint32_t data;
            rc = ptr_block_get(fs, mid, k, &data);
            if (rc != CFS_OK) return rc;
            if (data) {
                rc = block_free(fs, data);
                if (rc != CFS_OK) return rc;
                rc = ptr_block_set(fs, mid, k, 0u);
                if (rc != CFS_OK) return rc;
            }
        }
    }
    return cache_write(fs, n->double_indirect, fs->sector);
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
            inode_init_acl(&n);
            rc = inode_write(fs, i, &n);
            if (rc != CFS_OK) return rc;
            *id = i;
            return CFS_OK;
        }
    }
    return CFS_ENOSPC;
}

static int inode_release(Cfs *fs, uint32_t id) {
    CfsInode n;
    uint32_t b, count, lba;
    int rc = inode_read(fs, id, &n);
    if (rc != CFS_OK) return rc;
    if (n.type == CFS_INODE_FILE && n.size > 0u) {
        count = (n.size + STOR_SECTOR_SIZE - 1u) / STOR_SECTOR_SIZE;
        for (b = 0; b < count; b++) {
            if (file_lba(fs, &n, b, &lba, 0) == CFS_OK && lba) {
                rc = block_free(fs, lba);
                if (rc != CFS_OK) return rc;
            }
        }
    } else {
        for (b = 0; b < CFS_DIRECT_COUNT; b++) {
            if (n.direct[b]) {
                rc = block_free(fs, n.direct[b]);
                if (rc != CFS_OK) return rc;
            }
        }
    }
    rc = inode_ptr_free(fs, &n);
    if (rc != CFS_OK) return rc;
    bytes_zero(&n, (uint32_t)sizeof(n));
    return inode_write(fs, id, &n);
}

static int dir_find(Cfs *fs, uint32_t dir_id, const char *name,
                    uint32_t name_len, uint32_t *inode_id) {
    CfsInode dir;
    CfsDirent e;
    uint32_t b, slot;
    int rc = inode_read(fs, dir_id, &dir);
    if (rc != CFS_OK) return rc;
    if (dir.type != CFS_INODE_DIR) return CFS_ENOTDIR;
    for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
        uint32_t lba;
        rc = file_lba(fs, &dir, b, &lba, 0);
        if (rc != CFS_OK) {
            continue;
        }
        if (!data_lba_valid(lba)) {
            return CFS_ECORRUPT;
        }
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            return rc;
        }
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (e.inode && e.name_len && name_equal(&e, name, name_len)) {
                if (e.inode >= CFS_INODE_COUNT) {
                    return CFS_ECORRUPT;
                }
                *inode_id = e.inode;
                return CFS_OK;
            }
        }
    }
    return CFS_ENOENT;
}

static int dir_count(Cfs *fs, uint32_t dir_id, uint32_t *count) {
    CfsInode dir;
    CfsDirent e;
    uint32_t b, slot, n = 0u;
    int rc = inode_read(fs, dir_id, &dir);
    if (rc != CFS_OK) return rc;
    if (dir.type != CFS_INODE_DIR) return CFS_ENOTDIR;
    for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
        uint32_t lba;
        rc = file_lba(fs, &dir, b, &lba, 0);
        if (rc != CFS_OK) {
            continue;
        }
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            return rc;
        }
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (e.inode && e.name_len) {
                n++;
            }
        }
    }
    *count = n;
    return CFS_OK;
}

static int dir_add(Cfs *fs, uint32_t dir_id, const char *name,
                   uint32_t name_len, uint32_t inode_id, uint8_t type) {
    CfsInode dir;
    CfsDirent e;
    uint32_t b, slot;
    uint32_t target_lba = 0;
    uint32_t target_slot = 0u;
    int found = 0;
    int rc = inode_read(fs, dir_id, &dir);
    if (rc != CFS_OK) return rc;
    if (dir.type != CFS_INODE_DIR) return CFS_ENOTDIR;

    for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
        uint32_t lba;
        rc = file_lba(fs, &dir, b, &lba, 0);
        if (rc != CFS_OK) {
            continue;
        }
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            return rc;
        }
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (!e.inode && !found) {
                target_lba = lba;
                target_slot = slot;
                found = 1;
            }
        }
    }

    if (!found) {
        for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
            uint32_t lba;
            rc = file_lba(fs, &dir, b, &lba, 0);
            if (rc == CFS_OK) {
                continue;
            }
            rc = file_lba(fs, &dir, b, &lba, 1);
            if (rc != CFS_OK) {
                return rc;
            }
            rc = inode_write(fs, dir_id, &dir);
            if (rc != CFS_OK) {
                return rc;
            }
            target_lba = lba;
            target_slot = 0u;
            found = 1;
            break;
        }
        if (!found) {
            return CFS_ENOSPC;
        }
    }

    rc = cache_read(fs, target_lba, fs->sector);
    if (rc != CFS_OK) return rc;
    bytes_zero(&e, (uint32_t)sizeof(e));
    e.inode = inode_id;
    e.type = type;
    e.name_len = (uint8_t)name_len;
    for (b = 0; b < name_len; b++) e.name[b] = (uint8_t)name[b];
    cfs_dirent_encode(fs->sector + target_slot * CFS_DIRENT_SIZE, &e);
    rc = cache_write(fs, target_lba, fs->sector);
    if (rc != CFS_OK) return rc;
    dir.size += CFS_DIRENT_SIZE;
    return inode_write(fs, dir_id, &dir);
}

static int dir_remove(Cfs *fs, uint32_t dir_id, const char *name,
                      uint32_t name_len) {
    CfsInode dir;
    CfsDirent e;
    uint32_t b, slot;
    int rc = inode_read(fs, dir_id, &dir);
    if (rc != CFS_OK) return rc;
    if (dir.type != CFS_INODE_DIR) return CFS_ENOTDIR;
    for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
        uint32_t lba;
        rc = file_lba(fs, &dir, b, &lba, 0);
        if (rc != CFS_OK) {
            continue;
        }
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            return rc;
        }
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, fs->sector + slot * CFS_DIRENT_SIZE);
            if (e.inode && e.name_len && name_equal(&e, name, name_len)) {
                bytes_zero(&e, (uint32_t)sizeof(e));
                cfs_dirent_encode(fs->sector + slot * CFS_DIRENT_SIZE, &e);
                rc = cache_write(fs, lba, fs->sector);
                if (rc != CFS_OK) {
                    return rc;
                }
                if (dir.size >= CFS_DIRENT_SIZE) {
                    dir.size -= CFS_DIRENT_SIZE;
                }
                return inode_write(fs, dir_id, &dir);
            }
        }
    }
    return CFS_ENOENT;
}

static int walk_parent(Cfs *fs, const PathParts *p,
                       uint32_t *parent_id, uint32_t *leaf_index) {
    uint32_t cur = CFS_ROOT_INODE;
    uint32_t i, id;
    CfsInode in;
    int rc;
    if (p->ncomp == 0) return CFS_EINVAL;
    for (i = 0; i + 1 < p->ncomp; i++) {
        rc = dir_find(fs, cur, p->s + p->start[i], p->len[i], &id);
        if (rc != CFS_OK) return rc;
        rc = inode_read(fs, id, &in);
        if (rc != CFS_OK) return rc;
        if (in.type != CFS_INODE_DIR) return CFS_ENOTDIR;
        rc = cfs_perm_need(&in, CFS_PERM_WALK);
        if (rc != CFS_OK) return rc;
        cur = id;
    }
    *parent_id = cur;
    *leaf_index = p->ncomp - 1u;
    return CFS_OK;
}

static int walk_full(Cfs *fs, const PathParts *p, uint32_t *id_out) {
    uint32_t cur = CFS_ROOT_INODE;
    uint32_t i, id;
    CfsInode in;
    int rc;
    if (p->ncomp == 0) {
        *id_out = CFS_ROOT_INODE;
        return CFS_OK;
    }
    for (i = 0; i < p->ncomp; i++) {
        rc = dir_find(fs, cur, p->s + p->start[i], p->len[i], &id);
        if (rc != CFS_OK) return rc;
        rc = inode_read(fs, id, &in);
        if (rc != CFS_OK) return rc;
        if (i + 1 < p->ncomp && in.type != CFS_INODE_DIR)
            return CFS_ENOTDIR;
        if (i + 1 < p->ncomp) {
            rc = cfs_perm_need(&in, CFS_PERM_WALK);
            if (rc != CFS_OK) return rc;
        }
        cur = id;
    }
    *id_out = cur;
    return CFS_OK;
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
            inode_init_acl(&inode);
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
    super.journal_lba = CFS_JOURNAL_LBA;
    super.journal_sectors = CFS_JOURNAL_SECTORS;
    cfs_super_encode(sector, &super);
    if (bd_write(dev, CFS_SUPER_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;

    cfs_zero(sector, STOR_SECTOR_SIZE);
    cfs_put32(sector + 0, JNL_MAGIC);
    cfs_put32(sector + 8, JNL_EMPTY);
    cfs_put32(sector + 16, cfs_checksum(sector, 16u));
    if (bd_write(dev, CFS_JOURNAL_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;
    return io_error(bd_flush(dev));
}

int cfs_mount(Cfs *fs, BlockDevice *dev) {
    CfsInode root;
    uint8_t jhdr[STOR_SECTOR_SIZE];
    uint32_t jstate = JNL_EMPTY;
    uint32_t replayed = 0u;
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
    if (bd_read(dev, CFS_JOURNAL_LBA, 1u, jhdr) == BD_OK &&
        cfs_get32(jhdr + 0) == JNL_MAGIC &&
        cfs_get32(jhdr + 16) == cfs_checksum(jhdr, 16u))
        jstate = cfs_get32(jhdr + 8);
    rc = jnl_replay(fs, &replayed);
    if (rc != CFS_OK) {
        fs->mounted = 0u;
        return rc;
    }
    if (jstate == JNL_COMMIT)
        jnl_serial_mount(JNL_COMMIT, replayed);
    else if (jstate == JNL_BEGIN)
        jnl_serial_mount(JNL_BEGIN, 0u);
    else
        jnl_serial_mount(JNL_EMPTY, 0u);
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

int cfs_create(Cfs *fs, const char *path) {
    PathParts p;
    uint32_t parent, leaf, id, existing;
    CfsInode empty;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &p, &parent, &leaf);
    if (rc != CFS_OK) return rc;
    {
        CfsInode parent_in;
        rc = inode_read(fs, parent, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
    }
    rc = dir_find(fs, parent, p.s + p.start[leaf], p.len[leaf], &existing);
    if (rc == CFS_OK) return CFS_EEXIST;
    if (rc != CFS_ENOENT) return rc;
    rc = inode_alloc(fs, &id);
    if (rc != CFS_OK) return rc;
    rc = dir_add(fs, parent, p.s + p.start[leaf], p.len[leaf],
                 id, CFS_INODE_FILE);
    if (rc != CFS_OK) {
        bytes_zero(&empty, (uint32_t)sizeof(empty));
        (void)inode_write(fs, id, &empty);
        return rc;
    }
    fs->super.generation++;
    return (int)id;
}

int cfs_mkdir(Cfs *fs, const char *path) {
    PathParts p;
    uint32_t parent, leaf, id, existing;
    CfsInode node;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &p, &parent, &leaf);
    if (rc != CFS_OK) return rc;
    {
        CfsInode parent_in;
        rc = inode_read(fs, parent, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
    }
    rc = dir_find(fs, parent, p.s + p.start[leaf], p.len[leaf], &existing);
    if (rc == CFS_OK) return CFS_EEXIST;
    if (rc != CFS_ENOENT) return rc;
    rc = jnl_begin(&fs->jnl, fs);
    if (rc != CFS_OK) return rc;
    fs->jnl_active = 1u;
    fs->jnl_data = 0u;
    rc = inode_alloc(fs, &id);
    if (rc != CFS_OK) goto mkdir_out;
    rc = inode_read(fs, id, &node);
    if (rc != CFS_OK) goto mkdir_out;
    node.type = CFS_INODE_DIR;
    node.size = 0u;
    rc = inode_write(fs, id, &node);
    if (rc != CFS_OK) goto mkdir_out;
    rc = dir_add(fs, parent, p.s + p.start[leaf], p.len[leaf],
                 id, CFS_INODE_DIR);
    if (rc != CFS_OK) {
        bytes_zero(&node, (uint32_t)sizeof(node));
        (void)inode_write(fs, id, &node);
        goto mkdir_out;
    }
    fs->super.generation++;
mkdir_out:
    fs->jnl_active = 0u;
    if (rc != CFS_OK) return rc;
    return jnl_commit(&fs->jnl);
}

int cfs_rmdir(Cfs *fs, const char *path) {
    PathParts p;
    uint32_t parent, leaf, id, n;
    CfsInode node;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &p, &parent, &leaf);
    if (rc != CFS_OK) return rc;
    {
        CfsInode parent_in;
        rc = inode_read(fs, parent, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
    }
    rc = dir_find(fs, parent, p.s + p.start[leaf], p.len[leaf], &id);
    if (rc != CFS_OK) return rc;
    if (id == CFS_ROOT_INODE) return CFS_EINVAL;
    rc = inode_read(fs, id, &node);
    if (rc != CFS_OK) return rc;
    if (node.type != CFS_INODE_DIR) return CFS_ENOTDIR;
    rc = cfs_perm_need(&node, CFS_PERM_WRITE);
    if (rc != CFS_OK) return rc;
    rc = dir_count(fs, id, &n);
    if (rc != CFS_OK) return rc;
    if (n) return CFS_ENOTEMPTY;
    rc = dir_remove(fs, parent, p.s + p.start[leaf], p.len[leaf]);
    if (rc != CFS_OK) return rc;
    rc = inode_release(fs, id);
    if (rc != CFS_OK) return rc;
    fs->super.generation++;
    return CFS_OK;
}

int cfs_unlink(Cfs *fs, const char *path) {
    PathParts p;
    uint32_t parent, leaf, id;
    CfsInode node;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &p, &parent, &leaf);
    if (rc != CFS_OK) return rc;
    {
        CfsInode parent_in;
        rc = inode_read(fs, parent, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
    }
    rc = dir_find(fs, parent, p.s + p.start[leaf], p.len[leaf], &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &node);
    if (rc != CFS_OK) return rc;
    if (node.type != CFS_INODE_FILE) return CFS_EISDIR;
    rc = cfs_perm_need(&node, CFS_PERM_WRITE);
    if (rc != CFS_OK) return rc;
    rc = jnl_begin(&fs->jnl, fs);
    if (rc != CFS_OK) return rc;
    fs->jnl_active = 1u;
    fs->jnl_data = 0u;
    rc = dir_remove(fs, parent, p.s + p.start[leaf], p.len[leaf]);
    if (rc != CFS_OK) goto unlink_out;
    rc = inode_release(fs, id);
    if (rc != CFS_OK) goto unlink_out;
    fs->super.generation++;
unlink_out:
    fs->jnl_active = 0u;
    if (rc != CFS_OK) return rc;
    return jnl_commit(&fs->jnl);
}

int cfs_rename(Cfs *fs, const char *old_path, const char *new_path) {
    PathParts a, b;
    uint32_t pa, pb, la, lb, id, clash;
    CfsInode node;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    rc = parse_path(old_path, &a);
    if (rc != CFS_OK) return rc;
    rc = parse_path(new_path, &b);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &a, &pa, &la);
    if (rc != CFS_OK) return rc;
    rc = walk_parent(fs, &b, &pb, &lb);
    if (rc != CFS_OK) return rc;
    {
        CfsInode parent_in;
        rc = inode_read(fs, pa, &parent_in);
        if (rc != CFS_OK) return rc;
        rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
        if (rc != CFS_OK) return rc;
        if (pa != pb) {
            rc = inode_read(fs, pb, &parent_in);
            if (rc != CFS_OK) return rc;
            rc = cfs_perm_need(&parent_in, CFS_PERM_WRITE);
            if (rc != CFS_OK) return rc;
        }
    }
    rc = dir_find(fs, pa, a.s + a.start[la], a.len[la], &id);
    if (rc != CFS_OK) return rc;
    rc = dir_find(fs, pb, b.s + b.start[lb], b.len[lb], &clash);
    if (rc == CFS_OK) return CFS_EEXIST;
    if (rc != CFS_ENOENT) return rc;
    rc = inode_read(fs, id, &node);
    if (rc != CFS_OK) return rc;
    rc = dir_add(fs, pb, b.s + b.start[lb], b.len[lb], id, (uint8_t)node.type);
    if (rc != CFS_OK) return rc;
    rc = dir_remove(fs, pa, a.s + a.start[la], a.len[la]);
    if (rc != CFS_OK) return rc;
    fs->super.generation++;
    return CFS_OK;
}

int cfs_stat(Cfs *fs, const char *path, uint32_t *size, uint16_t *type) {
    PathParts p;
    uint32_t id;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!size && !type) return CFS_EINVAL;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (size) *size = inode.size;
    if (type) *type = inode.type;
    return CFS_OK;
}

int cfs_read(Cfs *fs, const char *path, void *out, uint32_t capacity) {
    PathParts p;
    uint32_t id, done = 0u, amount, block;
    uint8_t *dst = out;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!out && capacity) return CFS_EINVAL;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    if (p.ncomp == 0) return CFS_EINVAL;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (inode.type != CFS_INODE_FILE) return CFS_EISDIR;
    rc = cfs_perm_need(&inode, CFS_PERM_READ);
    if (rc != CFS_OK) return rc;
    if (inode.size > CFS_MAX_FILE_SIZE) return CFS_ECORRUPT;
    amount = inode.size < capacity ? inode.size : capacity;
    for (block = 0; done < amount; block++) {
        uint32_t take = amount - done;
        uint32_t lba;
        if (take > STOR_SECTOR_SIZE) take = STOR_SECTOR_SIZE;
        rc = file_lba(fs, &inode, block, &lba, 0);
        if (rc != CFS_OK) return rc;
        if (!data_lba_valid(lba)) return CFS_ECORRUPT;
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) return rc;
        bytes_copy(dst + done, fs->sector, take);
        done += take;
    }
    return (int)done;
}

int cfs_write(Cfs *fs, const char *path, const void *data, uint32_t size) {
    CfsInode old_inode, inode;
    PathParts p;
    uint32_t id, need, old_count, i, done, lba;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!data && size) return CFS_EINVAL;
    if (size > CFS_MAX_FILE_SIZE) return CFS_EFBIG;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    if (p.ncomp == 0) return CFS_EINVAL;
    rc = walk_full(fs, &p, &id);
    if (rc == CFS_ENOENT) {
        rc = cfs_create(fs, path);
        if (rc < 0) return rc;
        id = (uint32_t)rc;
    } else if (rc != CFS_OK) {
        return rc;
    }
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    if (inode.type != CFS_INODE_FILE ||
        inode.size > CFS_MAX_FILE_SIZE) return CFS_ECORRUPT;
    rc = cfs_perm_need(&inode, CFS_PERM_WRITE);
    if (rc != CFS_OK) return rc;
    old_inode = inode;
    old_count = (inode.size + STOR_SECTOR_SIZE - 1u) /
                STOR_SECTOR_SIZE;
    need = (size + STOR_SECTOR_SIZE - 1u) / STOR_SECTOR_SIZE;

    rc = jnl_begin(&fs->jnl, fs);
    if (rc != CFS_OK) return rc;
    fs->jnl_active = 1u;
    fs->jnl_data = 1u;

    done = 0u;
    for (i = 0; i < need; i++) {
        uint32_t take = size - done;
        if (take > STOR_SECTOR_SIZE) take = STOR_SECTOR_SIZE;
        rc = file_lba(fs, &inode, i, &lba, 1);
        if (rc != CFS_OK) goto out;
        bytes_zero(fs->sector, STOR_SECTOR_SIZE);
        bytes_copy(fs->sector, (const uint8_t *)data + done, take);
        rc = cache_write(fs, lba, fs->sector);
        if (rc != CFS_OK) goto out;
        done += take;
    }

    for (i = need; i < old_count; i++) {
        if (file_lba(fs, &old_inode, i, &lba, 0) == CFS_OK && lba) {
            rc = block_free(fs, lba);
            if (rc != CFS_OK) goto out;
        }
    }
    rc = inode_ptr_prune(fs, &inode, need);
    if (rc != CFS_OK) goto out;

    inode.size = size;
    inode.generation++;
    fs->jnl_data = 0u;
    rc = inode_write(fs, id, &inode);
    if (rc != CFS_OK) goto out;
    fs->super.generation++;
out:
    fs->jnl_active = 0u;
    fs->jnl_data = 0u;
    if (rc != CFS_OK) return rc;
    rc = jnl_commit(&fs->jnl);
    if (rc != CFS_OK) return rc;
    return (int)size;
}

int cfs_truncate(Cfs *fs, const char *path, uint32_t size) {
    uint32_t old_size;
    uint32_t i;
    uint16_t type;
    int rc;
    uint8_t *work;

    if (!fs || !fs->mounted) {
        return CFS_ENOTMOUNTED;
    }
    if (size > CFS_MAX_FILE_SIZE) {
        return CFS_EFBIG;
    }
    rc = cfs_stat(fs, path, &old_size, &type);
    if (rc != CFS_OK) {
        return rc;
    }
    if (type != CFS_INODE_FILE) {
        return CFS_EISDIR;
    }
    work = (uint8_t *)cfs_work_alloc(CFS_MAX_FILE_SIZE);
    if (!work) {
        return CFS_ENOSPC;
    }
    rc = cfs_read(fs, path, work, CFS_MAX_FILE_SIZE);
    if (rc < 0) {
        cfs_work_free(work);
        return rc;
    }
    for (i = old_size; i < size; i++) {
        work[i] = 0u;
    }
    rc = cfs_write(fs, path, work, size);
    cfs_work_free(work);
    return rc;
}

static int list_dir_inode(Cfs *fs, uint32_t dir_id, CfsListFn fn, void *ctx) {
    CfsInode dir, inode;
    CfsDirent e;
    char name[CFS_NAME_MAX + 1u];
    uint8_t dirsec[STOR_SECTOR_SIZE];
    uint32_t b, slot, i;
    int rc, count = 0;
    rc = inode_read(fs, dir_id, &dir);
    if (rc != CFS_OK) return rc;
    if (dir.type != CFS_INODE_DIR) return CFS_ENOTDIR;
    for (b = 0; b < CFS_DIR_MAX_BLOCKS; b++) {
        uint32_t lba;
        rc = file_lba(fs, &dir, b, &lba, 0);
        if (rc != CFS_OK) {
            continue;
        }
        rc = cache_read(fs, lba, dirsec);
        if (rc != CFS_OK) {
            return rc;
        }
        for (slot = 0; slot < CFS_DIRENTS_PER_SECTOR; slot++) {
            cfs_dirent_decode(&e, dirsec + slot * CFS_DIRENT_SIZE);
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

int cfs_list_at(Cfs *fs, const char *path, CfsListFn fn, void *ctx) {
    PathParts p;
    uint32_t id;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!fn) return CFS_EINVAL;
    if (!path) path = "";
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    return list_dir_inode(fs, id, fn, ctx);
}

int cfs_list(Cfs *fs, CfsListFn fn, void *ctx) {
    return cfs_list_at(fs, "", fn, ctx);
}

int cfs_perm(Cfs *fs, const char *path, uint32_t bit) {
    PathParts p;
    uint32_t id;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!path) return CFS_EINVAL;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    if (p.ncomp == 0) return CFS_EINVAL;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    return cfs_perm_need(&inode, bit);
}

int cfs_chmod(Cfs *fs, const char *path, uint32_t mode) {
    PathParts p;
    uint32_t id;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!path) return CFS_EINVAL;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    if (p.ncomp == 0) return CFS_EINVAL;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    inode.mode = mode & CFS_PERM_ALL;
    inode.flags = (uint16_t)inode.mode;
    return inode_write(fs, id, &inode);
}
