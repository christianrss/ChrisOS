#include "bdev.h"

static BlockDevice g_dev[BD_SLOTS];
static char g_name[BD_SLOTS][16];
static int g_n;
static int g_boot;

static void copy_name(char *dst, const char *src) {
    int i;
    for (i = 0; i < 15 && src && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

int bd_add(const char *name, const BlockDevice *dev) {
    if (!dev || g_n >= BD_SLOTS)
        return -1;
    g_dev[g_n] = *dev;
    copy_name(g_name[g_n], name ? name : "disk");
    g_n += 1;
    return g_n - 1;
}

int bd_count(void) {
    return g_n;
}

BlockDevice *bd_get(int index) {
    if (index < 0 || index >= g_n)
        return 0;
    return &g_dev[index];
}

const char *bd_name(int index) {
    if (index < 0 || index >= g_n)
        return "";
    return g_name[index];
}

int bd_boot_index(void) {
    return g_boot;
}

void bd_set_boot(int index) {
    g_boot = index;
}
