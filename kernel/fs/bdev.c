#include "bdev.h"

static BlockDevice g_dev[BD_SLOTS];
static char g_name[BD_SLOTS][16];
static int g_kind[BD_SLOTS];
static int g_flags[BD_SLOTS];
static int g_id[BD_SLOTS];
static int g_n;
static int g_boot;
static int g_next_id = 1;

static void copy_name(char *dst, const char *src) {
    int i;
    for (i = 0; i < 15 && src && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

int bd_add_kind(const char *name, const BlockDevice *dev, int kind) {
    if (!dev || g_n >= BD_SLOTS)
        return -1;
    g_dev[g_n] = *dev;
    copy_name(g_name[g_n], name ? name : "disk");
    g_kind[g_n] = kind;
    g_flags[g_n] = 0;
    g_id[g_n] = g_next_id++;
    g_n += 1;
    return g_n - 1;
}

int bd_add(const char *name, const BlockDevice *dev) {
    return bd_add_kind(name, dev, BD_OTHER);
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

int bd_kind(int index) {
    if (index < 0 || index >= g_n)
        return BD_OTHER;
    return g_kind[index];
}

int bd_flags(int index) {
    if (index < 0 || index >= g_n)
        return 0;
    return g_flags[index];
}

void bd_set_flags(int index, int flags) {
    if (index < 0 || index >= g_n)
        return;
    g_flags[index] = flags;
}

int bd_boot_index(void) {
    return g_boot;
}

void bd_set_boot(int index) {
    g_boot = index;
}

int bd_installable(int index) {
    BlockDevice *d = bd_get(index);
    int f;
    int k;
    if (!d || !d->writable)
        return 0;
    f = bd_flags(index);
    k = bd_kind(index);
    if (f & (BD_F_BOOT | BD_F_ROOT | BD_F_TEST))
        return 0;
    if (k == BD_RAM || k == BD_PARTITION)
        return 0;
    (void)g_id;
    return 1;
}
