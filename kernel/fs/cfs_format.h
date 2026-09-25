/* LEARN:ACL64-01 */
#ifndef CHRIS_CFS_FORMAT_H
#define CHRIS_CFS_FORMAT_H

#include <stdint.h>
#include "storage_limits.h"

enum {
    CFS_INODE_FREE = 0,
    CFS_INODE_FILE = 1,
    CFS_INODE_DIR = 2
};

#define CFS_PERM_READ  1u
#define CFS_PERM_WRITE 2u
#define CFS_PERM_EXEC  4u
#define CFS_PERM_WALK  8u
#define CFS_PERM_ALL   15u

typedef struct CfsSuper {
    uint16_t version;
    uint32_t generation;
    uint32_t clean;
    uint32_t journal_lba;
    uint32_t journal_sectors;
    uint32_t total_sectors;
    uint32_t bitmap_lba;
    uint32_t bitmap_sectors;
    uint32_t inode_lba;
    uint32_t inode_count;
    uint32_t data_lba;
    uint32_t data_sectors;
} CfsSuper;

typedef struct CfsInode {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t generation;
    uint32_t direct[CFS_DIRECT_COUNT];
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t triple_indirect;
    uint32_t mtime_lo;
    uint32_t mtime_hi;
} CfsInode;

typedef struct CfsDirent {
    uint32_t inode;
    uint8_t type;
    uint8_t name_len;
    uint16_t flags;
    uint8_t name[CFS_NAME_MAX];
} CfsDirent;

static inline uint16_t cfs_get16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static inline uint32_t cfs_get32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void cfs_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void cfs_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void cfs_zero(uint8_t *p, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) p[i] = 0u;
}

static inline uint32_t cfs_checksum(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static inline void cfs_super_legacy(CfsSuper *s, uint16_t version) {
    s->version = version;
    s->total_sectors = STOR_DISK_SECTORS;
    s->bitmap_lba = CFS_BITMAP_LBA;
    s->bitmap_sectors = CFS_BITMAP_SECTORS;
    s->inode_lba = CFS_INODE_LBA;
    s->inode_count = CFS_INODE_COUNT;
    s->journal_lba = CFS_JOURNAL_LBA;
    s->journal_sectors = CFS_JOURNAL_SECTORS;
    s->data_lba = CFS_DATA_LBA;
    s->data_sectors = CFS_DATA_SECTORS;
}

/* Bitmap grows with the disk. Inodes and the journal stay fixed, so a
 * 512 MiB volume keeps the v4 addresses (data at LBA 833). */
static inline int cfs_geom_for_count(uint32_t total, CfsSuper *s) {
    const uint64_t bits_per = (uint64_t)STOR_SECTOR_SIZE * 8ull;
    const uint64_t fixed = 1ull + (uint64_t)CFS_INODE_SECTORS +
                           (uint64_t)CFS_JOURNAL_SECTORS;
    uint64_t bitmap = 1ull;
    uint64_t data;
    uint32_t guard = 0u;
    if (!s || (uint64_t)total <= fixed + 1ull)
        return -1;
    for (;;) {
        uint64_t remain;
        uint64_t need;
        if (fixed + bitmap >= (uint64_t)total)
            return -1;
        remain = (uint64_t)total - fixed - bitmap;
        need = (remain + bits_per - 1ull) / bits_per;
        if (need == 0ull)
            need = 1ull;
        if (need <= bitmap) {
            data = remain;
            if (bitmap * bits_per < data)
                return -1;
            break;
        }
        bitmap = need;
        if (++guard > 8u)
            return -1;
    }
    if (bitmap > 0xffffffffull || data > 0xffffffffull)
        return -1;
    s->version = (uint16_t)CFS_VERSION;
    s->total_sectors = total;
    s->bitmap_lba = CFS_BITMAP_LBA;
    s->bitmap_sectors = (uint32_t)bitmap;
    s->inode_lba = s->bitmap_lba + s->bitmap_sectors;
    s->inode_count = CFS_INODE_COUNT;
    s->journal_sectors = CFS_JOURNAL_SECTORS;
    s->journal_lba = s->inode_lba + CFS_INODE_SECTORS;
    s->data_lba = s->journal_lba + s->journal_sectors;
    if ((uint64_t)s->data_lba + data != (uint64_t)total)
        return -1;
    s->data_sectors = (uint32_t)data;
    return 0;
}

static inline int cfs_super_geom_ok(const CfsSuper *s) {
    uint64_t bits;
    uint64_t end;
    if (!s || s->bitmap_sectors == 0u || s->data_sectors == 0u)
        return -1;
    if (s->inode_count != CFS_INODE_COUNT)
        return -1;
    if (s->journal_sectors < 2u)
        return -1;
    if (s->bitmap_lba == 0u || s->inode_lba <= s->bitmap_lba)
        return -1;
    if (s->bitmap_lba + s->bitmap_sectors > s->inode_lba)
        return -1;
    if ((uint64_t)s->inode_lba + CFS_INODE_SECTORS > s->journal_lba)
        return -1;
    if (s->data_lba < s->journal_lba + s->journal_sectors)
        return -1;
    end = (uint64_t)s->data_lba + s->data_sectors;
    if (end > s->total_sectors)
        return -1;
    bits = (uint64_t)s->bitmap_sectors * (uint64_t)STOR_SECTOR_SIZE * 8ull;
    if (bits < s->data_sectors)
        return -1;
    return 0;
}

static inline void cfs_super_encode(uint8_t out[512],
                                    const CfsSuper *s) {
    cfs_zero(out, 512u);
    cfs_put32(out + 0, CFS_MAGIC);
    cfs_put16(out + 4, s->version);
    cfs_put16(out + 6, (uint16_t)STOR_SECTOR_SIZE);
    cfs_put32(out + 8, s->total_sectors);
    cfs_put32(out + 12, s->bitmap_lba);
    cfs_put32(out + 16, s->bitmap_sectors);
    cfs_put32(out + 20, s->inode_lba);
    cfs_put32(out + 24, s->inode_count);
    cfs_put32(out + 28, CFS_INODE_SIZE);
    cfs_put32(out + 32, s->data_lba);
    cfs_put32(out + 36, s->data_sectors);
    cfs_put32(out + 40, CFS_ROOT_INODE);
    cfs_put32(out + 44, s->clean);
    cfs_put32(out + 48, s->generation);
    cfs_put32(out + 52, s->journal_lba);
    cfs_put32(out + 56, s->journal_sectors);
    cfs_put32(out + 60, cfs_checksum(out, 60u));
}

static inline int cfs_super_decode(CfsSuper *s,
                                   const uint8_t in[512]) {
    uint16_t ver;
    if (cfs_get32(in + 0) != CFS_MAGIC) return -1;
    ver = cfs_get16(in + 4);
    if (ver != CFS_VERSION && ver != CFS_VERSION_V4 &&
        ver != CFS_VERSION_COMPAT)
        return -2;
    if (cfs_get16(in + 6) != STOR_SECTOR_SIZE) return -3;
    if (cfs_get32(in + 60) != cfs_checksum(in, 60u)) return -14;
    s->clean = cfs_get32(in + 44);
    s->generation = cfs_get32(in + 48);
    if (ver == CFS_VERSION_V4 || ver == CFS_VERSION_COMPAT) {
        if (cfs_get32(in + 8) != STOR_DISK_SECTORS) return -4;
        if (cfs_get32(in + 12) != CFS_BITMAP_LBA) return -5;
        if (cfs_get32(in + 16) != CFS_BITMAP_SECTORS) return -6;
        if (cfs_get32(in + 20) != CFS_INODE_LBA) return -7;
        if (cfs_get32(in + 24) != CFS_INODE_COUNT) return -8;
        if (cfs_get32(in + 28) != CFS_INODE_SIZE) return -9;
        if (cfs_get32(in + 32) != CFS_DATA_LBA) return -10;
        if (cfs_get32(in + 36) != CFS_DATA_SECTORS) return -11;
        if (cfs_get32(in + 40) != CFS_ROOT_INODE) return -12;
        if (cfs_get32(in + 52) != CFS_JOURNAL_LBA) return -13;
        if (cfs_get32(in + 56) != CFS_JOURNAL_SECTORS) return -15;
        cfs_super_legacy(s, ver);
        s->clean = cfs_get32(in + 44);
        s->generation = cfs_get32(in + 48);
        return 0;
    }
    if (cfs_get32(in + 28) != CFS_INODE_SIZE) return -9;
    if (cfs_get32(in + 40) != CFS_ROOT_INODE) return -12;
    s->version = ver;
    s->total_sectors = cfs_get32(in + 8);
    s->bitmap_lba = cfs_get32(in + 12);
    s->bitmap_sectors = cfs_get32(in + 16);
    s->inode_lba = cfs_get32(in + 20);
    s->inode_count = cfs_get32(in + 24);
    s->data_lba = cfs_get32(in + 32);
    s->data_sectors = cfs_get32(in + 36);
    s->journal_lba = cfs_get32(in + 52);
    s->journal_sectors = cfs_get32(in + 56);
    if (cfs_super_geom_ok(s) != 0) return -4;
    return 0;
}

static inline void cfs_inode_encode(uint8_t out[CFS_INODE_SIZE],
                                    const CfsInode *n) {
    uint32_t i;
    cfs_zero(out, CFS_INODE_SIZE);
    cfs_put16(out + 0, n->type);
    cfs_put16(out + 2, n->flags);
    cfs_put32(out + 4, n->size);
    cfs_put32(out + 8, n->generation);
    for (i = 0; i < CFS_DIRECT_COUNT; i++)
        cfs_put32(out + 12u + i * 4u, n->direct[i]);
    cfs_put32(out + 60, n->indirect);
    cfs_put32(out + 64, n->double_indirect);
    cfs_put32(out + 68, n->uid);
    cfs_put32(out + 72, n->gid);
    cfs_put32(out + 76, n->mode);
    cfs_put32(out + 80, n->triple_indirect);
    cfs_put32(out + 84, n->mtime_lo);
    cfs_put32(out + 88, n->mtime_hi);
    cfs_put32(out + 124, cfs_checksum(out, 124u));
}

static inline int cfs_inode_decode(CfsInode *n,
                                   const uint8_t in[CFS_INODE_SIZE]) {
    uint32_t i;
    if (cfs_get32(in + 124) != cfs_checksum(in, 124u))
        return -1;
    n->type = cfs_get16(in + 0);
    n->flags = cfs_get16(in + 2);
    n->size = cfs_get32(in + 4);
    n->generation = cfs_get32(in + 8);
    for (i = 0; i < CFS_DIRECT_COUNT; i++)
        n->direct[i] = cfs_get32(in + 12u + i * 4u);
    n->indirect = cfs_get32(in + 60);
    n->double_indirect = cfs_get32(in + 64);
    n->uid = cfs_get32(in + 68);
    n->gid = cfs_get32(in + 72);
    n->mode = cfs_get32(in + 76);
    n->triple_indirect = cfs_get32(in + 80);
    n->mtime_lo = cfs_get32(in + 84);
    n->mtime_hi = cfs_get32(in + 88);
    return 0;
}

static inline void cfs_dirent_encode(uint8_t out[CFS_DIRENT_SIZE],
                                     const CfsDirent *e) {
    uint32_t i;
    cfs_zero(out, CFS_DIRENT_SIZE);
    cfs_put32(out + 0, e->inode);
    out[4] = e->type;
    out[5] = e->name_len;
    cfs_put16(out + 6, e->flags);
    for (i = 0; i < CFS_NAME_MAX; i++)
        out[8u + i] = e->name[i];
}

static inline void cfs_dirent_decode(CfsDirent *e,
                                     const uint8_t in[CFS_DIRENT_SIZE]) {
    uint32_t i;
    e->inode = cfs_get32(in + 0);
    e->type = in[4];
    e->name_len = in[5];
    e->flags = cfs_get16(in + 6);
    for (i = 0; i < CFS_NAME_MAX; i++)
        e->name[i] = in[8u + i];
}

#endif
