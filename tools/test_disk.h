#ifndef CHRIS_TEST_DISK_H
#define CHRIS_TEST_DISK_H

#include "storage_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *g_disk;

static int test_disk_init(void) {
    if (g_disk) {
        return 0;
    }
    g_disk = (unsigned char *)malloc((size_t)STOR_DISK_BYTES);
    if (!g_disk) {
        fprintf(stderr, "test_disk_init: need %u bytes\n",
                (unsigned)STOR_DISK_BYTES);
        return -1;
    }
    memset(g_disk, 0, (size_t)STOR_DISK_BYTES);
    return 0;
}

static void test_disk_reset(void) {
    if (g_disk) {
        memset(g_disk, 0, (size_t)STOR_DISK_BYTES);
    }
}

#endif
