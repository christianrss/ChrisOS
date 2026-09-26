#ifndef CHRIS_CLS_H
#define CHRIS_CLS_H

#include <stddef.h>
#include <stdint.h>

#define CLS_MAGIC 0x00534C43u /* 'CLS\0' little-endian */
#define CLS_NAME_MAX 32
#define CLS_MAX_EXPORTS 64
#define CLS_MAX_IMPORTS 32
#define CLS_MAX_TYPES 32
#define CLS_MAX_LIBS 16
#define CLS_MAX_CODE (512u * 1024u)

typedef struct ClsExport {
    char name[CLS_NAME_MAX];
    uint16_t sym_ver;
    uint16_t argc;
    uint32_t pc;
    uint32_t flags;
} ClsExport;

typedef struct ClsImport {
    char lib[CLS_NAME_MAX];
    char name[CLS_NAME_MAX];
    uint16_t need_major;
    uint16_t need_minor;
    uint16_t sym_ver;
    uint16_t pad;
} ClsImport;

typedef struct ClsType {
    char name[CLS_NAME_MAX];
    uint32_t size;
    uint32_t gc_bits;
} ClsType;

typedef struct ClsImage {
    char name[CLS_NAME_MAX];
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t nexports;
    uint16_t nimports;
    uint16_t ntypes;
    uint16_t pad;
    uint32_t code_size;
    uint32_t data_size;
    ClsExport exports[CLS_MAX_EXPORTS];
    ClsImport imports[CLS_MAX_IMPORTS];
    ClsType types[CLS_MAX_TYPES];
    const uint8_t *code;
    const uint8_t *data;
} ClsImage;

typedef struct ClsLoaded {
    int used;
    int lib_id;
    ClsImage img;
    uint8_t *code_buf;
    uint32_t code_cap;
    uint8_t *data_buf;
    uint32_t tramp[CLS_MAX_EXPORTS];
    uint32_t type_base;
} ClsLoaded;

int cls_parse(const uint8_t *file, size_t n, ClsImage *out);
size_t cls_write(uint8_t *out, size_t cap, const ClsImage *img);
int cls_abi_ok(uint16_t have_major, uint16_t have_minor, uint16_t need_major,
               uint16_t need_minor);
int cls_find_export(const ClsImage *img, const char *name, uint16_t sym_ver);

void cls_runtime_init(void);
int cls_runtime_load(const char *path, char *err, int err_cap);
int cls_runtime_reload(const char *path, char *err, int err_cap);
int cls_runtime_find(const char *name);
ClsLoaded *cls_runtime_get(int lib_id);
int cls_map_proc(int pid);
int cls_runtime_resolve(const char *lib, const char *name, uint16_t sym_ver,
                        uint32_t *pc_out, int *lib_out);
int cls_runtime_count(void);

#endif
