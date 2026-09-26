/* LEARN:WS64-W05 */
#ifndef CHRIS_CFS_H
#define CHRIS_CFS_H

#include <stdint.h>
#include "block_device.h"
#include "cfs_format.h"

#define CFS_PATH_MAX 512u
#define CFS_PATH_DEPTH 32u

#define JNL_MAGIC 0x4C4E4A43u
#define JNL_EMPTY 0u
#define JNL_BEGIN 1u
#define JNL_COMMIT 2u
#define JNL_MAX_REC 30u

enum {
    CFS_OK = 0,
    CFS_EINVAL = -20,
    CFS_EIO = -21,
    CFS_EFORMAT = -22,
    CFS_ENOENT = -23,
    CFS_EEXIST = -24,
    CFS_ENOSPC = -25,
    CFS_EFBIG = -26,
    CFS_ENAMETOOLONG = -27,
    CFS_ECORRUPT = -28,
    CFS_ENOTMOUNTED = -29,
    CFS_ENOTDIR = -30,
    CFS_ENOTEMPTY = -31,
    CFS_EISDIR = -32,
    CFS_EXDEV = -33,
    CFS_EPERM = -34
};

typedef struct PathParts {
    uint32_t ncomp;
    uint32_t start[CFS_PATH_DEPTH];
    uint32_t len[CFS_PATH_DEPTH];
    const char *s;
    uint32_t total;
} PathParts;

typedef struct CfsCacheLine {
    uint8_t data[STOR_SECTOR_SIZE];
    uint32_t lba;
    uint32_t age;
    uint8_t valid;
} CfsCacheLine;

typedef struct Cfs Cfs;

typedef struct Jnl {
    Cfs *fs;
    uint32_t seq;
    uint32_t nrec;
    uint32_t rec_lba[JNL_MAX_REC];
} Jnl;

typedef struct Cfs {
    BlockDevice *dev;
    CfsSuper super;
    CfsCacheLine cache[CFS_CACHE_LINES];
    uint8_t sector[STOR_SECTOR_SIZE];
    uint32_t clock;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint8_t mounted;
    Jnl jnl;
    uint8_t jnl_active;
    uint8_t jnl_data;
    /* Next bitmap index to try. block_alloc used to rescan from zero, so
     * copying a multi-megabyte file was quadratic and the install gate
     * never left the BOOT tree. */
    uint32_t alloc_hint;
} Cfs;

typedef int (*CfsListFn)(void *ctx, const char *name,
                         uint32_t size, uint16_t type);

int cfs_format(BlockDevice *dev);
int cfs_mount(Cfs *fs, BlockDevice *dev);
int cfs_sync(Cfs *fs);
int cfs_create(Cfs *fs, const char *path);
int cfs_mkdir(Cfs *fs, const char *path);
int cfs_rmdir(Cfs *fs, const char *path);
int cfs_unlink(Cfs *fs, const char *path);
int cfs_rename(Cfs *fs, const char *old_path, const char *new_path);
int cfs_read(Cfs *fs, const char *path, void *out, uint32_t capacity);
int cfs_read_at(Cfs *fs, const char *path, uint32_t offset, void *out,
                uint32_t capacity);
int cfs_write(Cfs *fs, const char *path, const void *data, uint32_t size);
int cfs_mtime(Cfs *fs, const char *path, uint64_t *out);
int cfs_write_at(Cfs *fs, const char *path, uint32_t offset,
                 const void *data, uint32_t size);
void cfs_set_now(uint64_t now);
int cfs_truncate(Cfs *fs, const char *path, uint32_t size);
int cfs_list(Cfs *fs, CfsListFn fn, void *ctx);
int cfs_list_at(Cfs *fs, const char *path, CfsListFn fn, void *ctx);
int cfs_stat(Cfs *fs, const char *path, uint32_t *size, uint16_t *type);
int cfs_fsck(Cfs *fs);
const char *cfs_fsck_reason(void);

int jnl_begin(Jnl *j, Cfs *fs);
int jnl_log(Jnl *j, uint32_t lba, const uint8_t data[512]);
int jnl_commit(Jnl *j);
int jnl_replay(Cfs *fs, uint32_t *replayed);
int cfs_perm(Cfs *fs, const char *path, uint32_t bit);
int cfs_chmod(Cfs *fs, const char *path, uint32_t mode);
uint64_t cfs_cache_hits(const Cfs *fs);
uint64_t cfs_cache_misses(const Cfs *fs);

#endif
