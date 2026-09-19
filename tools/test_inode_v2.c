/* tools/test_inode_v2.c */
#include "cfs_format.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t buf[CFS_INODE_SIZE];
    CfsInode a, b;
    memset(&a, 0, sizeof(a));
    a.type = CFS_INODE_FILE;
    a.size = 70000;
    a.direct[0] = CFS_DATA_LBA;
    a.indirect = CFS_DATA_LBA + 20u;
    a.double_indirect = CFS_DATA_LBA + 21u;
    cfs_inode_encode(buf, &a);
    if (cfs_inode_decode(&b, buf) != 0) return 1;
    if (b.size != 70000u || b.indirect != a.indirect) return 2;
    puts("test_inode_v2: ok");
    return 0;
}
