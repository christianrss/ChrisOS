/* LEARN:STOR64-S00 */
#ifndef CHRIS_STORAGE_LIMITS_H
#define CHRIS_STORAGE_LIMITS_H

#include <stdint.h>

#define STOR_SECTOR_SIZE       512u
#define STOR_DISK_SECTORS      16384u
#define STOR_DISK_BYTES        (STOR_SECTOR_SIZE * STOR_DISK_SECTORS)

#define CFS_MAGIC              0x31534643u /* bytes: C F S 1 */
#define CFS_VERSION            1u
#define CFS_SUPER_LBA          0u
#define CFS_BITMAP_LBA         1u
#define CFS_BITMAP_SECTORS     4u
#define CFS_INODE_LBA          5u
#define CFS_INODE_SECTORS      16u
#define CFS_DATA_LBA           21u
#define CFS_DATA_SECTORS       (STOR_DISK_SECTORS - CFS_DATA_LBA)

#define CFS_INODE_SIZE         64u
#define CFS_INODE_COUNT        128u
#define CFS_ROOT_INODE         0u
#define CFS_DIRECT_COUNT       12u
#define CFS_MAX_FILE_SIZE      (CFS_DIRECT_COUNT * STOR_SECTOR_SIZE)

#define CFS_DIRENT_SIZE        32u
#define CFS_NAME_MAX           24u
#define CFS_DIRENTS_PER_SECTOR (STOR_SECTOR_SIZE / CFS_DIRENT_SIZE)
#define CFS_ROOT_CAPACITY      (CFS_DIRECT_COUNT * CFS_DIRENTS_PER_SECTOR)

#define CFS_CACHE_LINES        8u

#if CFS_INODE_SIZE * CFS_INODE_COUNT != \
    CFS_INODE_SECTORS * STOR_SECTOR_SIZE
#error "inode table geometry is inconsistent"
#endif

#if CFS_DATA_SECTORS > CFS_BITMAP_SECTORS * STOR_SECTOR_SIZE * 8u
#error "bitmap cannot describe every data sector"
#endif

#if CFS_ROOT_CAPACITY < CFS_INODE_COUNT - 1u
#error "root directory cannot name every non-root inode"
#endif

#endif
