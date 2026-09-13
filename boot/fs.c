#include "fs.h"

FsFile fs_files[FS_MAX_FILES];
unsigned char fs_arena[FS_ARENA];
int fs_used;
int fs_bump;

static int fs_name_eq(const char *a, const char *b) {
    int i;
    for (i = 0; i < FS_NAME; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
    }
    return 1;
}

static void fs_name_copy(char *dst, const char *src) {
    int i;
    for (i = 0; i < FS_NAME - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

void fs_init(void) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs_files[i].used = 0;
        fs_files[i].name[0] = 0;
        fs_files[i].size = 0;
        fs_files[i].offset = 0;
        fs_files[i].type = 0;
    }
    fs_used = 0;
    fs_bump = 0;
}

int fs_find(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (fs_files[i].used && fs_name_eq(fs_files[i].name, name)) {
            return i;
        }
    }
    return -1;
}

int fs_create(const char *name, int type) {
    int i;
    if (fs_find(name) >= 0) {
        return -1;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_files[i].used) {
            fs_files[i].used = 1;
            fs_name_copy(fs_files[i].name, name);
            fs_files[i].type = type;
            fs_files[i].size = 0;
            fs_files[i].offset = 0;
            fs_used++;
            return i;
        }
    }
    return -1;
}