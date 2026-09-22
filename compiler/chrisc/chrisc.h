#ifndef CHRIS_CHRISC_H
#define CHRIS_CHRISC_H
#include <stddef.h>
#include <stdint.h>

#define CHRIS_SOURCE_MAX 4194304u
#define CHRIS_INC_MAX 262144u
#define CHRIS_MAP_MAX 8192
#define CHRIS_LST_MAX 128

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
    uint16_t abi_major;
    uint16_t abi_minor;
    int nexports;
    char export_name[64][32];
    uint32_t export_pc[64];
    uint16_t export_argc[64];
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
