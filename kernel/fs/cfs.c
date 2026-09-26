/* LEARN:WS64-W05 */
#include <stdint.h>
#include "cfs.h"
#include "fs_lock.h"

FsLock g_cfs_lock;

#ifdef __freestanding__
#include "smp.h"

uint32_t fs_lock_holder(void) {
    return smp_current_cpu() + 1u;
}
#else
uint32_t fs_lock_holder(void) __attribute__((weak));
uint32_t fs_lock_holder(void) {
    return 1u;
}
#endif

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
    fs->cache_hits = 0u;
    fs->cache_misses = 0u;
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
        fs->cache_hits++;
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
    fs->cache_misses++;
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

uint64_t cfs_cache_hits(const Cfs *fs) {
    CFS_LOCK();
    return fs ? fs->cache_hits : 0u;
}

uint64_t cfs_cache_misses(const Cfs *fs) {
    CFS_LOCK();
    return fs ? fs->cache_misses : 0u;
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
    return cache_write_raw(fs, fs->super.journal_lba, s);
}

int jnl_begin(Jnl *j, Cfs *fs) {
    uint8_t sector[STOR_SECTOR_SIZE];
    j->fs = fs;
    j->seq = fs->super.generation;
    j->nrec = 0u;
    fs->super.clean = 0u;
    cfs_super_encode(sector, &fs->super);
    (void)cache_write_raw(fs, CFS_SUPER_LBA, sector);
    return jnl_write_hdr(fs, JNL_BEGIN, j->seq, 0u);
}

int jnl_log(Jnl *j, uint32_t lba, const uint8_t data[512]) {
    uint8_t s[STOR_SECTOR_SIZE];
    uint32_t slot;
    if (j->nrec >= JNL_MAX_REC) return CFS_ENOSPC;
    slot = j->fs->super.journal_lba + 1u + j->nrec * 2u;
    cfs_zero(s, STOR_SECTOR_SIZE);
    cfs_put32(s + 0, lba);
    cfs_put32(s + 4, cfs_checksum(data, 512u));
    if (cache_write_raw(j->fs, slot, s) != CFS_OK) return CFS_EIO;
    if (cache_write_raw(j->fs, slot + 1u, data) != CFS_OK) return CFS_EIO;
    j->rec_lba[j->nrec++] = lba;
    return CFS_OK;
}

int jnl_commit(Jnl *j) {
    uint8_t sector[STOR_SECTOR_SIZE];
    int rc = jnl_write_hdr(j->fs, JNL_COMMIT, j->seq, j->nrec);
    if (rc != CFS_OK) return rc;
    rc = jnl_write_hdr(j->fs, JNL_EMPTY, j->seq + 1u, 0u);
    if (rc != CFS_OK) return rc;
    j->fs->super.clean = 1u;
    cfs_super_encode(sector, &j->fs->super);
    return cache_write_raw(j->fs, CFS_SUPER_LBA, sector);
}

int jnl_replay(Cfs *fs, uint32_t *replayed) {
    uint8_t hdr[STOR_SECTOR_SIZE];
    uint8_t meta[STOR_SECTOR_SIZE];
    uint8_t data[STOR_SECTOR_SIZE];
    uint32_t magic, state, nrec, i, slot, lba, sum;
    if (replayed) *replayed = 0u;
    if (cache_read(fs, fs->super.journal_lba, hdr) != CFS_OK) return CFS_EIO;
    magic = cfs_get32(hdr + 0);
    if (!magic) return CFS_OK;
    if (magic != JNL_MAGIC) return CFS_ECORRUPT;
    if (cfs_get32(hdr + 16) != cfs_checksum(hdr, 16u)) return CFS_ECORRUPT;
    state = cfs_get32(hdr + 8);
    nrec = cfs_get32(hdr + 12);
    if (state == JNL_EMPTY) return CFS_OK;
    if (state == JNL_BEGIN) {
        cfs_zero(hdr, STOR_SECTOR_SIZE);
        return cache_write_raw(fs, fs->super.journal_lba, hdr);
    }
    if (state != JNL_COMMIT) return CFS_ECORRUPT;
    for (i = 0; i < nrec && i < JNL_MAX_REC; i++) {
        slot = fs->super.journal_lba + 1u + i * 2u;
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
    return cache_write_raw(fs, fs->super.journal_lba, hdr);
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

/* Contiguous file reads. Touched only while g_cfs_lock is held. */
#define CFS_READAHEAD 16u
static uint8_t g_cfs_ra[CFS_READAHEAD * 512u];

static int data_lba_valid(const Cfs *fs, uint32_t lba) {
    return lba >= fs->super.data_lba &&
           lba < fs->super.data_lba + fs->super.data_sectors;
}

static int inode_read(Cfs *fs, uint32_t id, CfsInode *out) {
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    uint32_t lba, off;
    if (id >= fs->super.inode_count) return CFS_ECORRUPT;
    lba = fs->super.inode_lba + id / per_sector;
    off = (id % per_sector) * CFS_INODE_SIZE;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    if (cfs_inode_decode(out, fs->sector + off) != 0)
        return CFS_ECORRUPT;
    return CFS_OK;
}

static int inode_write(Cfs *fs, uint32_t id, const CfsInode *in) {
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;
    uint32_t lba, off;
    if (id >= fs->super.inode_count) return CFS_ECORRUPT;
    lba = fs->super.inode_lba + id / per_sector;
    off = (id % per_sector) * CFS_INODE_SIZE;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    cfs_inode_encode(fs->sector + off, in);
    return cache_write(fs, lba, fs->sector);
}

static int bitmap_set(Cfs *fs, uint32_t index, int used) {
    uint32_t lba, byte, bit;
    if (index >= fs->super.data_sectors) return CFS_ECORRUPT;
    lba = fs->super.bitmap_lba + index / (STOR_SECTOR_SIZE * 8u);
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
    if (index >= fs->super.data_sectors) return CFS_ECORRUPT;
    lba = fs->super.bitmap_lba + index / (STOR_SECTOR_SIZE * 8u);
    byte = (index / 8u) % STOR_SECTOR_SIZE;
    bit = index % 8u;
    if (cache_read(fs, lba, fs->sector) != CFS_OK) return CFS_EIO;
    *used = (fs->sector[byte] >> bit) & 1u;
    return CFS_OK;
}

static int block_alloc(Cfs *fs, uint32_t *lba) {
    uint32_t n;
    uint32_t start;
    int used, rc;
    start = fs->alloc_hint;
    if (start >= fs->super.data_sectors)
        start = 0u;
    for (n = 0; n < fs->super.data_sectors; n++) {
        uint32_t i = start + n;
        if (i >= fs->super.data_sectors)
            i -= fs->super.data_sectors;
        rc = bitmap_get(fs, i, &used);
        if (rc != CFS_OK) return rc;
        if (!used) {
            rc = bitmap_set(fs, i, 1);
            if (rc != CFS_OK) return rc;
            bytes_zero(fs->sector, STOR_SECTOR_SIZE);
            rc = cache_write(fs, fs->super.data_lba + i, fs->sector);
            if (rc != CFS_OK) {
                (void)bitmap_set(fs, i, 0);
                return rc;
            }
            fs->alloc_hint = i + 1u;
            *lba = fs->super.data_lba + i;
            return CFS_OK;
        }
    }
    return CFS_ENOSPC;
}

static int block_free(Cfs *fs, uint32_t lba) {
    uint32_t index;
    if (!data_lba_valid(fs, lba)) return CFS_ECORRUPT;
    index = lba - fs->super.data_lba;
    if (index < fs->alloc_hint)
        fs->alloc_hint = index;
    return bitmap_set(fs, index, 0);
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

static uint64_t g_cfs_clock = 1;

void cfs_set_now(uint64_t now) {
    CFS_LOCK();
    if (now != 0)
        g_cfs_clock = now;
}

static void inode_stamp(CfsInode *n) {
    g_cfs_clock++;
    n->mtime_lo = (uint32_t)g_cfs_clock;
    n->mtime_hi = (uint32_t)(g_cfs_clock >> 32);
}

static int file_lba(Cfs *fs, CfsInode *n, uint32_t block, uint32_t *lba,
                    int alloc) {
    uint32_t idx, mid, leaf, t;
    int rc;

    if (block >= CFS_MAX_BLOCKS_V4 || block >= fs->super.data_sectors) {
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
    if (idx >= (uint32_t)CFS_PTRS_PER_BLOCK * (uint32_t)CFS_PTRS_PER_BLOCK) {
        uint32_t tri = idx - (uint32_t)CFS_PTRS_PER_BLOCK * (uint32_t)CFS_PTRS_PER_BLOCK;
        uint32_t span = (uint32_t)CFS_PTRS_PER_BLOCK * (uint32_t)CFS_PTRS_PER_BLOCK;
        uint32_t hi = tri / span;
        uint32_t rest = tri % span;
        uint32_t mid2 = rest / CFS_PTRS_PER_BLOCK;
        uint32_t leaf2 = rest % CFS_PTRS_PER_BLOCK;
        uint32_t l1 = 0;
        uint32_t l2 = 0;
        uint32_t data = 0;
        if (!n->triple_indirect) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &n->triple_indirect);
            if (rc != CFS_OK) return rc;
        }
        rc = ptr_block_get(fs, n->triple_indirect, hi, &l1);
        if (rc != CFS_OK) return rc;
        if (!l1) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &l1);
            if (rc != CFS_OK) return rc;
            rc = ptr_block_set(fs, n->triple_indirect, hi, l1);
            if (rc != CFS_OK) return rc;
        }
        rc = ptr_block_get(fs, l1, mid2, &l2);
        if (rc != CFS_OK) return rc;
        if (!l2) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &l2);
            if (rc != CFS_OK) return rc;
            rc = ptr_block_set(fs, l1, mid2, l2);
            if (rc != CFS_OK) return rc;
        }
        rc = ptr_block_get(fs, l2, leaf2, &data);
        if (rc != CFS_OK) return rc;
        if (!data) {
            if (!alloc) return CFS_ECORRUPT;
            rc = block_alloc(fs, &data);
            if (rc != CFS_OK) return rc;
            rc = ptr_block_set(fs, l2, leaf2, data);
            if (rc != CFS_OK) return rc;
        }
        *lba = data;
        return CFS_OK;
    }
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

static int free_ptr_levels(Cfs *fs, uint32_t lba, int levels) {
    uint8_t buf[STOR_SECTOR_SIZE];
    uint32_t i;
    int rc;
    if (!lba) return CFS_OK;
    if (levels <= 1) return block_free(fs, lba);
    rc = cache_read(fs, lba, buf);
    if (rc != CFS_OK) return rc;
    for (i = 0; i < CFS_PTRS_PER_BLOCK; i++) {
        uint32_t child = cfs_get32(buf + i * 4u);
        if (!child) continue;
        rc = free_ptr_levels(fs, child, levels - 1);
        if (rc != CFS_OK) return rc;
    }
    return block_free(fs, lba);
}

static int inode_ptr_free(Cfs *fs, CfsInode *n) {
    uint32_t k, mid;
    uint8_t dbl[STOR_SECTOR_SIZE];
    int rc;
    if (n->indirect) {
        rc = block_free(fs, n->indirect);
        if (rc != CFS_OK) return rc;
        n->indirect = 0u;
    }
    if (n->double_indirect) {
        /* Local copy: block_free/bitmap_set clobber fs->sector. */
        rc = cache_read(fs, n->double_indirect, dbl);
        if (rc != CFS_OK) return rc;
        for (k = 0; k < CFS_PTRS_PER_BLOCK; k++) {
            mid = cfs_get32(dbl + k * 4u);
            if (mid) {
                rc = block_free(fs, mid);
                if (rc != CFS_OK) return rc;
            }
        }
        rc = block_free(fs, n->double_indirect);
        if (rc != CFS_OK) return rc;
        n->double_indirect = 0u;
    }
    if (n->triple_indirect) {
        rc = free_ptr_levels(fs, n->triple_indirect, 3);
        if (rc != CFS_OK) return rc;
        n->triple_indirect = 0u;
    }
    return CFS_OK;
}

static int inode_ptr_prune(Cfs *fs, CfsInode *n, uint32_t need) {
    uint32_t i, idx, mid_idx, leaf_idx, mid, k;
    uint8_t dbl[STOR_SECTOR_SIZE];
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
            rc = cache_read(fs, n->double_indirect, dbl);
            if (rc != CFS_OK) return rc;
            for (k = 0; k < CFS_PTRS_PER_BLOCK; k++) {
                mid = cfs_get32(dbl + k * 4u);
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
    /* Keep double-indirect table in a local buffer — helpers reuse fs->sector. */
    rc = cache_read(fs, n->double_indirect, dbl);
    if (rc != CFS_OK) return rc;
    for (i = mid_idx + 1u; i < CFS_PTRS_PER_BLOCK; i++) {
        mid = cfs_get32(dbl + i * 4u);
        if (mid) {
            rc = block_free(fs, mid);
            if (rc != CFS_OK) return rc;
            cfs_put32(dbl + i * 4u, 0u);
        }
    }
    mid = cfs_get32(dbl + mid_idx * 4u);
    if (mid) {
        if (leaf_idx == 0u) {
            /* Entire mid block unused: free it and drop the pointer. */
            rc = block_free(fs, mid);
            if (rc != CFS_OK) return rc;
            cfs_put32(dbl + mid_idx * 4u, 0u);
        } else {
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
    }
    return cache_write(fs, n->double_indirect, dbl);
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
        if (!data_lba_valid(fs, lba)) {
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
    CFS_LOCK();
    uint8_t sector[STOR_SECTOR_SIZE];
    CfsSuper super;
    CfsInode inode;
    uint32_t lba, i, j;
    uint32_t per_sector = STOR_SECTOR_SIZE / CFS_INODE_SIZE;

    if (!dev || dev->sector_size != STOR_SECTOR_SIZE || !dev->writable)
        return CFS_EINVAL;
    bytes_zero(&super, (uint32_t)sizeof(super));
    if (cfs_geom_for_count(dev->sector_count, &super) != 0)
        return CFS_EINVAL;
    super.clean = 1u;
    super.generation = 1u;

    bytes_zero(sector, STOR_SECTOR_SIZE);
    for (lba = CFS_SUPER_LBA; lba < super.data_lba; lba++)
        if (bd_write(dev, lba, 1u, sector) != BD_OK) return CFS_EIO;

    sector[0] = 1u;
    if (bd_write(dev, super.bitmap_lba, 1u, sector) != BD_OK)
        return CFS_EIO;

    bytes_zero(&inode, (uint32_t)sizeof(inode));
    for (i = 0; i < CFS_INODE_SECTORS; i++) {
        bytes_zero(sector, STOR_SECTOR_SIZE);
        for (j = 0; j < per_sector; j++)
            cfs_inode_encode(sector + j * CFS_INODE_SIZE, &inode);
        if (i == 0u) {
            inode.type = CFS_INODE_DIR;
            inode.generation = 1u;
            inode.direct[0] = super.data_lba;
            inode_init_acl(&inode);
            cfs_inode_encode(sector, &inode);
            bytes_zero(&inode, (uint32_t)sizeof(inode));
        }
        if (bd_write(dev, super.inode_lba + i, 1u, sector) != BD_OK)
            return CFS_EIO;
    }

    bytes_zero(sector, STOR_SECTOR_SIZE);
    if (bd_write(dev, super.data_lba, 1u, sector) != BD_OK)
        return CFS_EIO;

    cfs_super_encode(sector, &super);
    if (bd_write(dev, CFS_SUPER_LBA, 1u, sector) != BD_OK)
        return CFS_EIO;

    cfs_zero(sector, STOR_SECTOR_SIZE);
    cfs_put32(sector + 0, JNL_MAGIC);
    cfs_put32(sector + 8, JNL_EMPTY);
    cfs_put32(sector + 16, cfs_checksum(sector, 16u));
    if (bd_write(dev, super.journal_lba, 1u, sector) != BD_OK)
        return CFS_EIO;
    return io_error(bd_flush(dev));
}

int cfs_mount(Cfs *fs, BlockDevice *dev) {
    CFS_LOCK();
    CfsInode root;
    uint8_t jhdr[STOR_SECTOR_SIZE];
    uint32_t jstate = JNL_EMPTY;
    uint32_t replayed = 0u;
    int rc;
    if (!fs || !dev || dev->sector_size != STOR_SECTOR_SIZE ||
        dev->sector_count == 0u)
        return CFS_EINVAL;
    bytes_zero(fs, (uint32_t)sizeof(*fs));
    fs->dev = dev;
    if (bd_read(dev, CFS_SUPER_LBA, 1u, fs->sector) != BD_OK)
        return CFS_EIO;
    if (cfs_super_decode(&fs->super, fs->sector) != 0)
        return CFS_EFORMAT;
    if (dev->sector_count < fs->super.total_sectors)
        return CFS_EINVAL;
    cache_reset(fs);
    fs->mounted = 1u;
    if (bd_read(dev, fs->super.journal_lba, 1u, jhdr) == BD_OK &&
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
    CFS_LOCK();
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    return io_error(bd_flush(fs->dev));
}

int cfs_create(Cfs *fs, const char *path) {
    CFS_LOCK();
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
    CFS_LOCK();
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
    CFS_LOCK();
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
    CFS_LOCK();
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
    CFS_LOCK();
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
    CFS_LOCK();
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

int cfs_read_at(Cfs *fs, const char *path, uint32_t offset, void *out,
                uint32_t capacity) {
    CFS_LOCK();
    PathParts p;
    uint32_t id, done = 0u, amount, block, sector_off;
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
    if (inode.size > CFS_MAX_FILE_BYTES) return CFS_ECORRUPT;
    if (offset >= inode.size)
        return 0;
    amount = inode.size - offset;
    if (amount > capacity)
        amount = capacity;
    block = offset / STOR_SECTOR_SIZE;
    sector_off = offset % STOR_SECTOR_SIZE;
    while (done < amount) {
        uint32_t take = amount - done;
        uint32_t lba;
        uint32_t available = STOR_SECTOR_SIZE - sector_off;
        uint32_t run;
        uint32_t bi;
        if (sector_off == 0u && take >= STOR_SECTOR_SIZE) {
            rc = file_lba(fs, &inode, block, &lba, 0);
            if (rc != CFS_OK) return rc;
            if (!data_lba_valid(fs, lba)) return CFS_ECORRUPT;
            run = 1u;
            while (run < CFS_READAHEAD &&
                   done + (run + 1u) * STOR_SECTOR_SIZE <= amount) {
                uint32_t next = 0;
                int nrc = file_lba(fs, &inode, block + run, &next, 0);
                if (nrc != CFS_OK || !data_lba_valid(fs, next) || next != lba + run)
                    break;
                run++;
            }
            if (run > 1u) {
                if (bd_read(fs->dev, lba, run, g_cfs_ra) != BD_OK)
                    return CFS_EIO;
                for (bi = 0; bi < run; bi++)
                    cache_drop_lba(fs, lba + bi);
                bytes_copy(dst + done, g_cfs_ra, run * STOR_SECTOR_SIZE);
                done += run * STOR_SECTOR_SIZE;
                block += run;
                continue;
            }
        }
        if (take > available) take = available;
        rc = file_lba(fs, &inode, block, &lba, 0);
        if (rc != CFS_OK) return rc;
        if (!data_lba_valid(fs, lba)) return CFS_ECORRUPT;
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) return rc;
        bytes_copy(dst + done, fs->sector + sector_off, take);
        done += take;
        block++;
        sector_off = 0u;
    }
    return (int)done;
}

int cfs_read(Cfs *fs, const char *path, void *out, uint32_t capacity) {
    CFS_LOCK();
    return cfs_read_at(fs, path, 0u, out, capacity);
}

int cfs_write(Cfs *fs, const char *path, const void *data, uint32_t size) {
    CFS_LOCK();
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
    inode_stamp(&inode);
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

int cfs_mtime(Cfs *fs, const char *path, uint64_t *out) {
    CFS_LOCK();
    PathParts p;
    uint32_t id;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted || !out) return CFS_EINVAL;
    rc = parse_path(path, &p);
    if (rc != CFS_OK) return rc;
    rc = walk_full(fs, &p, &id);
    if (rc != CFS_OK) return rc;
    rc = inode_read(fs, id, &inode);
    if (rc != CFS_OK) return rc;
    *out = (uint64_t)inode.mtime_lo | ((uint64_t)inode.mtime_hi << 32);
    return CFS_OK;
}

int cfs_write_at(Cfs *fs, const char *path, uint32_t offset,
                 const void *data, uint32_t size) {
    CFS_LOCK();
    PathParts p;
    uint32_t id, done, block, sector_off, end;
    const uint8_t *src = data;
    CfsInode inode;
    int rc;
    if (!fs || !fs->mounted) return CFS_ENOTMOUNTED;
    if (!data && size) return CFS_EINVAL;
    if (size > CFS_MAX_FILE_BYTES || offset > CFS_MAX_FILE_BYTES - size)
        return CFS_EFBIG;
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
    if (inode.type != CFS_INODE_FILE) return CFS_EISDIR;
    rc = cfs_perm_need(&inode, CFS_PERM_WRITE);
    if (rc != CFS_OK) return rc;
    end = offset + size;
    if (end > inode.size)
        inode.size = end;
    inode_stamp(&inode);
    rc = jnl_begin(&fs->jnl, fs);
    if (rc != CFS_OK) return rc;
    fs->jnl_active = 1u;
    fs->jnl_data = 1u;
    block = offset / STOR_SECTOR_SIZE;
    sector_off = offset % STOR_SECTOR_SIZE;
    done = 0u;
    while (done < size) {
        uint32_t take = size - done;
        uint32_t lba;
        uint32_t room = STOR_SECTOR_SIZE - sector_off;
        uint32_t k;
        if (take > room) take = room;
        rc = file_lba(fs, &inode, block, &lba, 1);
        if (rc != CFS_OK) {
            fs->jnl_active = 0u;
            return rc;
        }
        rc = cache_read(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            fs->jnl_active = 0u;
            return rc;
        }
        for (k = 0; k < take; k++)
            fs->sector[sector_off + k] = src[done + k];
        rc = cache_write(fs, lba, fs->sector);
        if (rc != CFS_OK) {
            fs->jnl_active = 0u;
            return rc;
        }
        done += take;
        block++;
        sector_off = 0u;
    }
    fs->jnl_data = 0u;
    rc = inode_write(fs, id, &inode);
    fs->jnl_active = 0u;
    if (rc != CFS_OK) return rc;
    return jnl_commit(&fs->jnl) == CFS_OK ? (int)size : CFS_EIO;
}

int cfs_truncate(Cfs *fs, const char *path, uint32_t size) {
    CFS_LOCK();
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
    CFS_LOCK();
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
    CFS_LOCK();
    return cfs_list_at(fs, "", fn, ctx);
}

int cfs_perm(Cfs *fs, const char *path, uint32_t bit) {
    CFS_LOCK();
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
    CFS_LOCK();
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
