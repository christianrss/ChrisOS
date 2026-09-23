/* LEARN:SH16-01 */
#ifndef CHRIS_STORAGE_LIMITS_H
#define CHRIS_STORAGE_LIMITS_H

#include <stdint.h>

#define STOR_SECTOR_SIZE       512u
#define STOR_DISK_SECTORS      1048576u
#define STOR_DISK_BYTES        (STOR_SECTOR_SIZE * STOR_DISK_SECTORS)

#define CFS_MAGIC              0x31534643u
#define CFS_VERSION            4u
#define CFS_VERSION_COMPAT     3u
#define CFS_SUPER_LBA          0u
#define CFS_BITMAP_LBA         1u
#define CFS_BITMAP_SECTORS     256u
#define CFS_INODE_LBA          257u
#define CFS_INODE_SECTORS      512u
#define CFS_JOURNAL_LBA        769u
#define CFS_JOURNAL_SECTORS    64u
#define CFS_DATA_LBA           (CFS_JOURNAL_LBA + CFS_JOURNAL_SECTORS)
#define CFS_DATA_SECTORS       (STOR_DISK_SECTORS - CFS_DATA_LBA)

#define CFS_INODE_SIZE         128u
#define CFS_INODE_COUNT        2048u
#define CFS_ROOT_INODE         0u
#define CFS_DIRECT_COUNT       12u
#define CFS_PTRS_PER_BLOCK     (STOR_SECTOR_SIZE / 4u)
/* 12 direct + 128 singly + 128^2 doubly. Buffered cfs_write stays here. */
#define CFS_MAX_BLOCKS         ((uint32_t)CFS_DIRECT_COUNT + \
                                (uint32_t)CFS_PTRS_PER_BLOCK + \
                                (uint32_t)CFS_PTRS_PER_BLOCK * \
                                (uint32_t)CFS_PTRS_PER_BLOCK)
/* 16524 * 512 = 8460288 bytes (~8.06 MiB); whole-file API */
#define CFS_MAX_FILE_SIZE      (CFS_MAX_BLOCKS * STOR_SECTOR_SIZE)
#define CFS_TRIPLE_BLOCKS      ((uint32_t)CFS_PTRS_PER_BLOCK * \
                                (uint32_t)CFS_PTRS_PER_BLOCK * \
                                (uint32_t)CFS_PTRS_PER_BLOCK)
#define CFS_MAX_BLOCKS_V4      (CFS_MAX_BLOCKS + CFS_TRIPLE_BLOCKS)
#define CFS_MAX_FILE_BYTES     (CFS_DATA_SECTORS * STOR_SECTOR_SIZE)

#define CFS_DIRENT_SIZE        80u
#define CFS_NAME_MAX           64u
#define CFS_DIRENTS_PER_SECTOR (STOR_SECTOR_SIZE / CFS_DIRENT_SIZE)
#define CFS_CACHE_LINES        64u

#if CFS_INODE_SIZE * CFS_INODE_COUNT != CFS_INODE_SECTORS * STOR_SECTOR_SIZE
#error "inode table geometry is inconsistent"
#endif

#if CFS_DATA_SECTORS > CFS_BITMAP_SECTORS * STOR_SECTOR_SIZE * 8u
#error "bitmap cannot describe every data sector"
#endif

#if CFS_DIRENT_SIZE < 8u + CFS_NAME_MAX
#error "dirent too small for name field"
#endif

#endif
