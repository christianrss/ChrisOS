#ifndef CHRIS_FS_H
#define CHRIS_FS_H

#define FS_MAX_FILES 32
#define FS_NAME 12
#define FS_ARENA 262144

#define FS_TEXT     1
#define FS_CLVM     2
#define FS_BUILTIN  3

typedef struct FsFile {
    char name[FS_NAME];
    int type;
    int size;
    int offset; /* arena, ou id de builtin se type=FS_BUILTIN */
    int used;
} FsFile;

void fs_init(void);
int fs_find(const char *name);
int fs_create(const char *name, int type);

extern FsFile fs_files[FS_MAX_FILES];
extern unsigned char fs_arena[FS_ARENA];
extern int fs_used;
extern int fs_bump;

#endif