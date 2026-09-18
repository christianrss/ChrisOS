/* LEARN:WS64-W05 */
#include "fs.h"

#include "cfs.h"
#include "serial.h"
#include "storage.h"

typedef struct RamFile {
    char name[FS_NAME];
    int size;
    int offset;
    int used;
} RamFile;

static int g_backend;
static RamFile g_ram_files[FS_MAX_FILES];
static unsigned char g_ram_arena[FS_ARENA];
static int g_ram_used;
static int g_ram_bump;

static int ram_name_len(const char *name) {
    int n = 0;
    if (!name) {
        return -1;
    }
    while (name[n]) {
        if (name[n] == '/' || name[n] == '\\') {
            return -1;
        }
        if (n >= FS_NAME - 1) {
            return -2;
        }
        n++;
    }
    if (n < 1) {
        return -1;
    }
    return n;
}

static int ram_name_eq(const char *a, const char *b) {
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

static void ram_name_copy(char *dst, const char *src) {
    int i;
    for (i = 0; i < FS_NAME - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static void ram_reset(void) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        g_ram_files[i].used = 0;
        g_ram_files[i].name[0] = 0;
        g_ram_files[i].size = 0;
        g_ram_files[i].offset = 0;
    }
    g_ram_used = 0;
    g_ram_bump = 0;
}

static int ram_find(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_ram_files[i].used && ram_name_eq(g_ram_files[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int ram_write(const char *name, const unsigned char *data, int n) {
    int id;
    int i;
    int nl = ram_name_len(name);
    if (nl == -2) {
        return CFS_ENAMETOOLONG;
    }
    if (nl < 1 || n < 0 || (!data && n > 0)) {
        return CFS_EINVAL;
    }
    id = ram_find(name);
    if (id < 0) {
        for (i = 0; i < FS_MAX_FILES; i++) {
            if (!g_ram_files[i].used) {
                id = i;
                break;
            }
        }
        if (id < 0) {
            return CFS_ENOSPC;
        }
        if (g_ram_bump + n > FS_ARENA) {
            return CFS_ENOSPC;
        }
        g_ram_files[id].used = 1;
        ram_name_copy(g_ram_files[id].name, name);
        g_ram_used++;
    } else if (g_ram_bump + n > FS_ARENA) {
        return CFS_ENOSPC;
    }
    g_ram_files[id].offset = g_ram_bump;
    g_ram_files[id].size = n;
    for (i = 0; i < n; i++) {
        g_ram_arena[g_ram_bump + i] = data[i];
    }
    g_ram_bump += n;
    return n;
}

static int ram_read(const char *name, unsigned char *out, int out_cap) {
    int id;
    int i;
    int n;
    int nl = ram_name_len(name);
    if (nl == -2) {
        return CFS_ENAMETOOLONG;
    }
    if (nl < 1 || !out || out_cap < 0) {
        return CFS_EINVAL;
    }
    id = ram_find(name);
    if (id < 0) {
        return CFS_ENOENT;
    }
    n = g_ram_files[id].size;
    if (n > out_cap) {
        n = out_cap;
    }
    for (i = 0; i < n; i++) {
        out[i] = g_ram_arena[g_ram_files[id].offset + i];
    }
    return n;
}

static int ram_list(FsListFn fn, void *ctx) {
    int i;
    int count = 0;
    int rc;
    if (!fn) {
        return CFS_EINVAL;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!g_ram_files[i].used) {
            continue;
        }
        rc = fn(ctx, g_ram_files[i].name, (uint32_t)g_ram_files[i].size,
                CFS_INODE_FILE);
        if (rc) {
            return rc;
        }
        count++;
    }
    return count;
}

static void seed_dirs(Cfs *fs) {
    int rc;
    rc = cfs_mkdir(fs, "GAMES");
    if (rc != CFS_OK && rc != CFS_EEXIST) {
        serial_puts("fs mkdir GAMES failed\n");
    }
    rc = cfs_mkdir(fs, "SRC");
    if (rc != CFS_OK && rc != CFS_EEXIST) {
        serial_puts("fs mkdir SRC failed\n");
    }
    rc = cfs_mkdir(fs, "BIN");
    if (rc != CFS_OK && rc != CFS_EEXIST) {
        serial_puts("fs mkdir BIN failed\n");
    }
}

void fs_init(void) {
    Cfs *fs;
    if (storage_ready() && storage_cfs()) {
        g_backend = FS_BACKEND_CFS;
        serial_puts("fs backend cfs64\n");
        fs = storage_cfs();
        if (fs) {
            seed_dirs(fs);
            (void)cfs_sync(fs);
        }
        return;
    }
    g_backend = FS_BACKEND_RAM;
    ram_reset();
    serial_puts("fs backend ram\n");
}

int fs_backend(void) {
    return g_backend;
}

int fs_write(const char *path, const void *data, int n) {
    Cfs *fs;
    if (g_backend == FS_BACKEND_CFS) {
        fs = storage_cfs();
        if (!fs) {
            return CFS_ENOTMOUNTED;
        }
        if (n < 0) {
            return CFS_EINVAL;
        }
        return cfs_write(fs, path, data, (uint32_t)n);
    }
    return ram_write(path, (const unsigned char *)data, n);
}

int fs_read(const char *path, void *out, int out_cap) {
    Cfs *fs;
    if (g_backend == FS_BACKEND_CFS) {
        fs = storage_cfs();
        if (!fs) {
            return CFS_ENOTMOUNTED;
        }
        if (out_cap < 0) {
            return CFS_EINVAL;
        }
        return cfs_read(fs, path, out, (uint32_t)out_cap);
    }
    return ram_read(path, (unsigned char *)out, out_cap);
}

int fs_mkdir(const char *path) {
    Cfs *fs;
    if (g_backend != FS_BACKEND_CFS) {
        return CFS_EINVAL;
    }
    fs = storage_cfs();
    if (!fs) {
        return CFS_ENOTMOUNTED;
    }
    return cfs_mkdir(fs, path);
}

int fs_rmdir(const char *path) {
    Cfs *fs;
    if (g_backend != FS_BACKEND_CFS) {
        return CFS_EINVAL;
    }
    fs = storage_cfs();
    if (!fs) {
        return CFS_ENOTMOUNTED;
    }
    return cfs_rmdir(fs, path);
}

int fs_unlink(const char *path) {
    Cfs *fs;
    if (g_backend != FS_BACKEND_CFS) {
        return CFS_EINVAL;
    }
    fs = storage_cfs();
    if (!fs) {
        return CFS_ENOTMOUNTED;
    }
    return cfs_unlink(fs, path);
}

int fs_rename(const char *old_path, const char *new_path) {
    Cfs *fs;
    if (g_backend != FS_BACKEND_CFS) {
        return CFS_EINVAL;
    }
    fs = storage_cfs();
    if (!fs) {
        return CFS_ENOTMOUNTED;
    }
    return cfs_rename(fs, old_path, new_path);
}

int fs_stat(const char *path, uint32_t *size, uint16_t *type) {
    Cfs *fs;
    int id;
    if (g_backend == FS_BACKEND_CFS) {
        fs = storage_cfs();
        if (!fs) {
            return CFS_ENOTMOUNTED;
        }
        return cfs_stat(fs, path, size, type);
    }
    id = ram_find(path);
    if (id < 0) {
        return CFS_ENOENT;
    }
    if (size) {
        *size = (uint32_t)g_ram_files[id].size;
    }
    if (type) {
        *type = CFS_INODE_FILE;
    }
    return CFS_OK;
}

int fs_list_at(const char *path, FsListFn fn, void *ctx) {
    Cfs *fs;
    if (!fn) {
        return CFS_EINVAL;
    }
    if (g_backend == FS_BACKEND_CFS) {
        fs = storage_cfs();
        if (!fs) {
            return CFS_ENOTMOUNTED;
        }
        return cfs_list_at(fs, path, fn, ctx);
    }
    if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
        return CFS_EINVAL;
    }
    return ram_list(fn, ctx);
}

int fs_list(FsListFn fn, void *ctx) {
    return fs_list_at("", fn, ctx);
}
