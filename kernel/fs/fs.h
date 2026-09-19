/* LEARN:WS64-W05 */
#ifndef CHRIS_FS_H
#define CHRIS_FS_H

#include <stdint.h>

#define FS_BACKEND_RAM 0
#define FS_BACKEND_CFS 1

#define FS_MAX_FILES 32
#define FS_NAME 12
#define FS_PATH 512
#define FS_ARENA 262144

void fs_init(void);
int fs_backend(void);
int fs_write(const char *path, const void *data, int n);
int fs_read(const char *path, void *out, int out_cap);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_unlink(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_stat(const char *path, uint32_t *size, uint16_t *type);
typedef int (*FsListFn)(void *ctx, const char *name, uint32_t size,
                        uint16_t type);
int fs_list(FsListFn fn, void *ctx);
int fs_list_at(const char *path, FsListFn fn, void *ctx);
void fs_err_status(char *out, int cap, int rc, const char *prefix);

#endif
