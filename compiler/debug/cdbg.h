#ifndef CHRIS_CDBG_H
#define CHRIS_CDBG_H

#include <stddef.h>
#include <stdint.h>

#define CDBG_VERSION 1
#define CDBG_FILES 32
#define CDBG_LINES 256
#define CDBG_FUNCS 64
#define CDBG_PATH 96
#define CDBG_NAME 32

typedef struct CdbgFile {
    uint16_t id;
    char path[CDBG_PATH];
} CdbgFile;

typedef struct CdbgLine {
    uint32_t pc_start;
    uint32_t pc_end;
    uint16_t file_id;
    uint16_t line;
    uint16_t column;
} CdbgLine;

typedef struct CdbgFunc {
    char name[CDBG_NAME];
    uint32_t start;
    uint32_t end;
    uint16_t file_id;
    uint16_t line;
    uint16_t argc;
} CdbgFunc;

typedef struct CdbgImage {
    uint16_t version;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t exec_hash;
    uint32_t source_hash;
    int nfiles;
    int nlines;
    int nfuncs;
    CdbgFile files[CDBG_FILES];
    CdbgLine lines[CDBG_LINES];
    CdbgFunc funcs[CDBG_FUNCS];
} CdbgImage;

struct ChrisResult;

int cdbg_encode(uint8_t *out, int cap, const CdbgImage *img);
int cdbg_decode(const uint8_t *in, int n, CdbgImage *img);
int cdbg_line_at(const CdbgImage *img, uint32_t pc, CdbgLine *out);
int cdbg_func_at(const CdbgImage *img, uint32_t pc, CdbgFunc *out);
int cdbg_file_at(const CdbgImage *img, uint32_t pc, char *path, int cap);

int cdbg_bound(const struct ChrisResult *result);
int cdbg_from_result(uint8_t *out, int cap, const struct ChrisResult *result,
                     const uint8_t *code, uint32_t code_n, uint32_t source_hash);

/* One .MAP text record. Returns 1 when the line is "pc line [file]". */
int cdbg_parse_map_line(const char *s, uint32_t *pc, uint16_t *line,
                        uint16_t *file);

#endif
