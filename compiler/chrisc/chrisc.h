#ifndef CHRIS_CHRISC_H
#define CHRIS_CHRISC_H
#include <stddef.h>
#include <stdint.h>

#define CHRIS_SOURCE_MAX 4194304u
#define CHRIS_INC_MAX 262144u
#define CHRIS_MAP_MAX 8192
#define CHRIS_LST_MAX 128
#define CHRIS_DIAG_MAX 8
#define CHRIS_FILE_MAX 32
#define CHRIS_SEV_ERROR 1
#define CHRIS_SEV_WARNING 2
#define CHRIS_SEV_NOTE 3

typedef struct ChrisDiag {
    int line, column;
    char file[64];
    char message[80];
} ChrisDiag;

typedef struct ChrisDiagnostic {
    int severity;
    int code;
    char file[64];
    int line;
    int column;
    int end_line;
    int end_column;
    char message[80];
} ChrisDiagnostic;

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
    int diag_n;
    ChrisDiagnostic diags[CHRIS_DIAG_MAX];
    int nfiles;
    char file_path[CHRIS_FILE_MAX][64];
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
typedef void (*ChriscProgressFn)(void *user, int index, int total,
                                const char *path);

int chrisc_diag_push(ChrisResult *result, int severity, int code,
                     const char *file, int line, int column, int end_line,
                     int end_column, const char *message);
int chrisc_compile(const char *source, size_t source_size,
                   uint8_t *code, size_t code_cap, ChrisResult *result);
int chrisc_compile_ex(const char *path, const char *source, size_t source_size,
                      ChriscReadFn read, void *user,
                      uint8_t *code, size_t code_cap, ChrisResult *result);
int chrisc_compile_files(const char **paths, int npaths, ChriscReadFn read,
                         void *user, uint8_t *code, size_t code_cap,
                         ChrisResult *result);
int chrisc_compile_files_ex(const char **paths, int npaths, ChriscReadFn read,
                            void *user, uint8_t *code, size_t code_cap,
                            ChrisResult *result, ChriscProgressFn progress,
                            void *progress_user);
void chrisc_set_yield(void (*fn)(void));
#endif
