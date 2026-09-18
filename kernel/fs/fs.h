/* LEARN:STOR64-S07 */
#ifndef CHRIS_FS_H
#define CHRIS_FS_H

#include <stdint.h>

#define FS_BACKEND_RAM 0
#define FS_BACKEND_CFS 1

#define FS_MAX_FILES 32
#define FS_NAME 12
#define FS_ARENA 262144

void fs_init(void);
int fs_backend(void);
int fs_write(const char *name, const void *data, int n);
int fs_read(const char *name, void *out, int out_cap);
typedef int (*FsListFn)(void *ctx, const char *name, uint32_t size);
int fs_list(FsListFn fn, void *ctx);

#endif
