#ifndef SYS_STAT_H
#define SYS_STAT_H
struct stat {
    int st_size;
};
int mkdir(char *path, int mode);
int stat(char *path, struct stat *st);
#endif
