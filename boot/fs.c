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

int fs_write(const char *name, const unsigned char *data, int n, int type) {
    int id = fs_find(name);
    int i;
    if (n < 0 || n > FS_ARENA) {
        return -1;
    }
    if (fs_bump + n > FS_ARENA) {
        return -2;
    }
    if (id < 0) {
        id = fs_create(name, type);
        if (id < 0) {
            return -1;
        }
    }
    fs_files[id].type = type;
    fs_files[id].offset = fs_bump;
    fs_files[id].size = n;
    for (i = 0; i < n; i++) {
        fs_arena[fs_bump + i] = data[i];
    }
    fs_bump += n;
    return id;
}

int fs_read(const char *name, unsigned char *out, int out_cap) {
    int id = fs_find(name);
    int i, n;
    if (id < 0) {
        return -1;
    }
    n = fs_files[id].size;
    if (n > out_cap) {
        n = out_cap;
    }
    for (i = 0; i < n; i++) {
        out[i] = fs_arena[fs_files[id].offset + i];
    }
    return n;
}