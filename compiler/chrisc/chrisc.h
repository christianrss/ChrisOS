#ifndef CHRIS_CHRISC_H
#define CHRIS_CHRISC_H
#include <stddef.h>
#include <stdint.h>

#define CHRIS_SOURCE_MAX 524288u
#define CHRIS_MAP_MAX 2048

typedef struct ChrisDiag {
    int line, column;
    char file[64];
    char message[80];
} ChrisDiag;

typedef struct ChrisMapEnt {
    uint32_t pc;
    uint16_t line;
    uint16_t file_id;
} ChrisMapEnt;

typedef struct ChrisResult {
    size_t code_size;
    uint32_t entry;
    unsigned variables;
    ChrisDiag diag;
    int map_n;
    ChrisMapEnt map[CHRIS_MAP_MAX];
} ChrisResult;

typedef int (*ChriscReadFn)(void *user, const char *path, char *out, int cap);

int chrisc_compile(const char *source, size_t source_size,
                   uint8_t *code, size_t code_cap, ChrisResult *result);
int chrisc_compile_ex(const char *path, const char *source, size_t source_size,
                      ChriscReadFn read, void *user,
                      uint8_t *code, size_t code_cap, ChrisResult *result);
int chrisc_compile_files(const char **paths, int npaths, ChriscReadFn read,
                         void *user, uint8_t *code, size_t code_cap,
                         ChrisResult *result);
#endif
