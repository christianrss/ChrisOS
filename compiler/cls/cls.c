#include "cls.h"
#include "gc/gc.h"

#ifdef __freestanding__
#include "bootinfo.h"
#include "fs.h"
#include "heap.h"
#include "mm.h"
#include "proc.h"
#define CLS_ALLOC kmalloc
#define CLS_FREE kfree
#else
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CLS_ALLOC malloc
#define CLS_FREE free
#endif

static ClsLoaded g_libs[CLS_MAX_LIBS];
static int g_nlibs;
static uint32_t g_next_type_base = 1;

static void cpy(char *d, size_t n, const char *s) {
    size_t i = 0;
    if (!n)
        return;
    while (s && s[i] && i + 1 < n) {
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
}

static int same(const char *a, const char *b) {
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static uint32_t ru32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t ru16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wu32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wu16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int cls_abi_ok(uint16_t have_major, uint16_t have_minor, uint16_t need_major,
               uint16_t need_minor) {
    if (have_major != need_major)
        return 0;
    return have_minor >= need_minor;
}

int cls_find_export(const ClsImage *img, const char *name, uint16_t sym_ver) {
    int i;
    if (!img || !name)
        return -1;
    for (i = 0; i < (int)img->nexports; ++i) {
        if (same(img->exports[i].name, name) &&
            (sym_ver == 0 || img->exports[i].sym_ver == sym_ver))
            return i;
    }
    return -1;
}

int cls_parse(const uint8_t *file, size_t n, ClsImage *out) {
    uint32_t magic;
    uint32_t off;
    uint16_t i;
    if (!file || !out || n < 64)
        return 0;
    magic = ru32(file);
    if (magic != CLS_MAGIC)
        return 0;
    out->abi_major = ru16(file + 4);
    out->abi_minor = ru16(file + 6);
    out->nexports = ru16(file + 8);
    out->nimports = ru16(file + 10);
    out->ntypes = ru16(file + 12);
    out->pad = ru16(file + 14);
    out->code_size = ru32(file + 16);
    out->data_size = ru32(file + 20);
    for (i = 0; i < CLS_NAME_MAX; ++i)
        out->name[i] = (char)file[24 + i];
    out->name[CLS_NAME_MAX - 1] = 0;
    if (out->nexports > CLS_MAX_EXPORTS || out->nimports > CLS_MAX_IMPORTS ||
        out->ntypes > CLS_MAX_TYPES)
        return 0;
    off = 56;
    for (i = 0; i < out->nexports; ++i) {
        if (off + 44 > n)
            return 0;
        cpy(out->exports[i].name, CLS_NAME_MAX, (const char *)file + off);
        out->exports[i].sym_ver = ru16(file + off + 32);
        out->exports[i].argc = ru16(file + off + 34);
        out->exports[i].pc = ru32(file + off + 36);
        out->exports[i].flags = ru32(file + off + 40);
        off += 44;
    }
    for (i = 0; i < out->nimports; ++i) {
        if (off + 72 > n)
            return 0;
        cpy(out->imports[i].lib, CLS_NAME_MAX, (const char *)file + off);
        cpy(out->imports[i].name, CLS_NAME_MAX, (const char *)file + off + 32);
        out->imports[i].need_major = ru16(file + off + 64);
        out->imports[i].need_minor = ru16(file + off + 66);
        out->imports[i].sym_ver = ru16(file + off + 68);
        out->imports[i].pad = ru16(file + off + 70);
        off += 72;
    }
    for (i = 0; i < out->ntypes; ++i) {
        if (off + 40 > n)
            return 0;
        cpy(out->types[i].name, CLS_NAME_MAX, (const char *)file + off);
        out->types[i].size = ru32(file + off + 32);
        out->types[i].gc_bits = ru32(file + off + 36);
        off += 40;
    }
    if (off + out->code_size + out->data_size > n)
        return 0;
    out->code = file + off;
    out->data = file + off + out->code_size;
    return 1;
}

size_t cls_write(uint8_t *out, size_t cap, const ClsImage *img) {
    size_t need;
    uint32_t off;
    uint16_t i;
    uint16_t k;
    if (!out || !img)
        return 0;
    need = 56u + (size_t)img->nexports * 44u + (size_t)img->nimports * 72u +
           (size_t)img->ntypes * 40u + img->code_size + img->data_size;
    if (need > cap)
        return 0;
    wu32(out, CLS_MAGIC);
    wu16(out + 4, img->abi_major);
    wu16(out + 6, img->abi_minor);
    wu16(out + 8, img->nexports);
    wu16(out + 10, img->nimports);
    wu16(out + 12, img->ntypes);
    wu16(out + 14, 0);
    wu32(out + 16, img->code_size);
    wu32(out + 20, img->data_size);
    for (k = 0; k < CLS_NAME_MAX; ++k)
        out[24 + k] = (uint8_t)(img->name[k] ? img->name[k] : 0);
    off = 56;
    for (i = 0; i < img->nexports; ++i) {
        for (k = 0; k < CLS_NAME_MAX; ++k)
            out[off + k] =
                (uint8_t)(img->exports[i].name[k] ? img->exports[i].name[k]
                                                  : 0);
        wu16(out + off + 32, img->exports[i].sym_ver);
        wu16(out + off + 34, img->exports[i].argc);
        wu32(out + off + 36, img->exports[i].pc);
        wu32(out + off + 40, img->exports[i].flags);
        off += 44;
    }
    for (i = 0; i < img->nimports; ++i) {
        for (k = 0; k < CLS_NAME_MAX; ++k)
            out[off + k] =
                (uint8_t)(img->imports[i].lib[k] ? img->imports[i].lib[k] : 0);
        for (k = 0; k < CLS_NAME_MAX; ++k)
            out[off + 32 + k] =
                (uint8_t)(img->imports[i].name[k] ? img->imports[i].name[k]
                                                  : 0);
        wu16(out + off + 64, img->imports[i].need_major);
        wu16(out + off + 66, img->imports[i].need_minor);
        wu16(out + off + 68, img->imports[i].sym_ver);
        wu16(out + off + 70, 0);
        off += 72;
    }
    for (i = 0; i < img->ntypes; ++i) {
        for (k = 0; k < CLS_NAME_MAX; ++k)
            out[off + k] =
                (uint8_t)(img->types[i].name[k] ? img->types[i].name[k] : 0);
        wu32(out + off + 32, img->types[i].size);
        wu32(out + off + 36, img->types[i].gc_bits);
        off += 40;
    }
    if (img->code && img->code_size) {
        for (k = 0; k < img->code_size; ++k)
            out[off + k] = img->code[k];
    }
    off += img->code_size;
    if (img->data && img->data_size) {
        for (k = 0; k < img->data_size; ++k)
            out[off + k] = img->data[k];
    }
    return need;
}

void cls_runtime_init(void) {
    int i;
    for (i = 0; i < CLS_MAX_LIBS; ++i) {
        if (g_libs[i].code_buf)
            CLS_FREE(g_libs[i].code_buf);
        if (g_libs[i].data_buf)
            CLS_FREE(g_libs[i].data_buf);
        g_libs[i].used = 0;
        g_libs[i].code_buf = 0;
        g_libs[i].data_buf = 0;
    }
    g_nlibs = 0;
    g_next_type_base = 1;
    gc_type_clear();
}

static void set_err(char *err, int err_cap, const char *msg) {
    int i = 0;
    if (!err || err_cap < 1)
        return;
    while (msg[i] && i + 1 < err_cap) {
        err[i] = msg[i];
        i++;
    }
    err[i] = 0;
}

static int install_types(ClsLoaded *L) {
    uint16_t i;
    L->type_base = g_next_type_base;
    for (i = 0; i < L->img.ntypes; ++i) {
        uint32_t tid = L->type_base + i;
        if (!gc_type_register(tid, L->img.types[i].size, L->img.types[i].gc_bits,
                              L->img.types[i].name))
            return 0;
    }
    g_next_type_base += L->img.ntypes ? L->img.ntypes : 1;
    return 1;
}

static int check_imports(const ClsImage *img, char *err, int err_cap) {
    uint16_t i;
    for (i = 0; i < img->nimports; ++i) {
        int lib = cls_runtime_find(img->imports[i].lib);
        ClsLoaded *L;
        int ex;
        if (lib < 0) {
            set_err(err, err_cap, "cls: missing import lib");
            return 0;
        }
        L = cls_runtime_get(lib);
        if (!cls_abi_ok(L->img.abi_major, L->img.abi_minor,
                        img->imports[i].need_major, img->imports[i].need_minor)) {
            set_err(err, err_cap, "cls: abi mismatch");
            return 0;
        }
        ex = cls_find_export(&L->img, img->imports[i].name,
                             img->imports[i].sym_ver);
        if (ex < 0) {
            set_err(err, err_cap, "cls: missing export");
            return 0;
        }
    }
    return 1;
}

static int load_bytes(const uint8_t *file, size_t n, const char *path,
                      int replace_id, char *err, int err_cap) {
    ClsImage img;
    ClsLoaded *L;
    uint32_t i;
    int slot;
    (void)path;
    if (!cls_parse(file, n, &img)) {
        set_err(err, err_cap, "cls: bad image");
        return -1;
    }
    if (img.code_size > CLS_MAX_CODE) {
        set_err(err, err_cap, "cls: code too big");
        return -1;
    }
    if (!check_imports(&img, err, err_cap))
        return -1;
    if (replace_id >= 0) {
        L = &g_libs[replace_id];
        if (L->used && L->img.abi_major != img.abi_major) {
            set_err(err, err_cap, "cls: reload major mismatch");
            return -1;
        }
        slot = replace_id;
        if (L->data_buf &&
            (L->img.data_size != img.data_size || L->img.abi_minor > img.abi_minor)) {
            CLS_FREE(L->data_buf);
            L->data_buf = 0;
        }
    } else {
        slot = -1;
        for (i = 0; i < CLS_MAX_LIBS; ++i) {
            if (!g_libs[i].used) {
                slot = (int)i;
                break;
            }
            if (same(g_libs[i].img.name, img.name)) {
                set_err(err, err_cap, "cls: already loaded");
                return -1;
            }
        }
        if (slot < 0) {
            set_err(err, err_cap, "cls: table full");
            return -1;
        }
        L = &g_libs[slot];
        L->data_buf = 0;
    }
    {
        uint32_t need = img.code_size ? img.code_size : 1;
        int reuse = replace_id >= 0 && L->code_buf && L->code_cap >= need;
        if (reuse) {
            for (i = 0; i < img.code_size; ++i)
                L->code_buf[i] = img.code[i];
        } else {
            uint8_t *fresh = (uint8_t *)CLS_ALLOC(need);
            uint8_t *old = L->code_buf;
            if (!fresh) {
                set_err(err, err_cap, "cls: oom");
                return -1;
            }
            for (i = 0; i < img.code_size; ++i)
                fresh[i] = img.code[i];
            L->code_buf = fresh;
            L->code_cap = need;
            if (replace_id >= 0 && old) {
#ifdef __freestanding__
                int pid;
                for (pid = 1; pid < PROC_MAX; ++pid) {
                    if (proc_alive(pid))
                        cls_map_proc(pid);
                }
#endif
                CLS_FREE(old);
            }
        }
    }
    if (img.data_size) {
        int preserve = replace_id >= 0 && L->data_buf &&
                       L->img.data_size == img.data_size &&
                       L->img.abi_major == img.abi_major;
        if (!preserve) {
            if (L->data_buf)
                CLS_FREE(L->data_buf);
            L->data_buf = (uint8_t *)CLS_ALLOC(img.data_size);
            if (!L->data_buf) {
                CLS_FREE(L->code_buf);
                L->code_buf = 0;
                set_err(err, err_cap, "cls: oom data");
                return -1;
            }
            for (i = 0; i < img.data_size; ++i)
                L->data_buf[i] = img.data[i];
        }
    }
    L->img = img;
    L->img.code = L->code_buf;
    L->img.data = L->data_buf;
    L->used = 1;
    L->lib_id = slot;
    for (i = 0; i < img.nexports; ++i)
        L->tramp[i] = img.exports[i].pc;
    if (replace_id < 0) {
        if (!install_types(L)) {
            set_err(err, err_cap, "cls: type register fail");
            return -1;
        }
        g_nlibs++;
    }
    return slot;
}

int cls_runtime_load(const char *path, char *err, int err_cap) {
#ifdef __freestanding__
    static uint8_t buf[CLS_MAX_CODE + 65536];
    int n;
    if (!path) {
        set_err(err, err_cap, "cls: null path");
        return -1;
    }
    n = fs_read(path, buf, (int)sizeof(buf));
    if (n < 0) {
        set_err(err, err_cap, "cls: not found");
        return -1;
    }
    return load_bytes(buf, (size_t)n, path, -1, err, err_cap);
#else
    FILE *f;
    uint8_t *buf;
    long sz;
    int rc;
    (void)path;
    f = fopen(path, "rb");
    if (!f) {
        set_err(err, err_cap, "cls: not found");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (long)(CLS_MAX_CODE + 65536)) {
        fclose(f);
        set_err(err, err_cap, "cls: bad size");
        return -1;
    }
    buf = (uint8_t *)CLS_ALLOC((size_t)sz);
    if (!buf) {
        fclose(f);
        set_err(err, err_cap, "cls: oom");
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        CLS_FREE(buf);
        fclose(f);
        set_err(err, err_cap, "cls: read fail");
        return -1;
    }
    fclose(f);
    rc = load_bytes(buf, (size_t)sz, path, -1, err, err_cap);
    CLS_FREE(buf);
    return rc;
#endif
}

int cls_runtime_reload(const char *path, char *err, int err_cap) {
    static uint8_t buf[CLS_MAX_CODE + 65536];
    ClsImage probe;
    int n;
    int lib;
#ifdef __freestanding__
    if (!path) {
        set_err(err, err_cap, "cls: null path");
        return -1;
    }
    n = fs_read(path, buf, (int)sizeof(buf));
    if (n < 0) {
        set_err(err, err_cap, "cls: not found");
        return -1;
    }
#else
    FILE *f;
    long sz;
    if (!path) {
        set_err(err, err_cap, "cls: null path");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        set_err(err, err_cap, "cls: not found");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (long)sizeof(buf)) {
        fclose(f);
        set_err(err, err_cap, "cls: bad size");
        return -1;
    }
    n = (int)fread(buf, 1, (size_t)sz, f);
    fclose(f);
#endif
    if (!cls_parse(buf, (size_t)n, &probe)) {
        set_err(err, err_cap, "cls: bad image");
        return -1;
    }
    lib = cls_runtime_find(probe.name);
    if (lib < 0)
        return load_bytes(buf, (size_t)n, path, -1, err, err_cap);
    return load_bytes(buf, (size_t)n, path, lib, err, err_cap);
}

int cls_runtime_find(const char *name) {
    int i;
    for (i = 0; i < CLS_MAX_LIBS; ++i) {
        if (g_libs[i].used && same(g_libs[i].img.name, name))
            return i;
    }
    return -1;
}

ClsLoaded *cls_runtime_get(int lib_id) {
    if (lib_id < 0 || lib_id >= CLS_MAX_LIBS || !g_libs[lib_id].used)
        return 0;
    return &g_libs[lib_id];
}

int cls_runtime_resolve(const char *lib, const char *name, uint16_t sym_ver,
                        uint32_t *pc_out, int *lib_out) {
    int id = cls_runtime_find(lib);
    ClsLoaded *L;
    int ex;
    if (id < 0)
        return 0;
    L = &g_libs[id];
    ex = cls_find_export(&L->img, name, sym_ver);
    if (ex < 0)
        return 0;
    if (pc_out)
        *pc_out = L->tramp[ex];
    if (lib_out)
        *lib_out = id;
    return 1;
}

int cls_runtime_count(void) {
    return g_nlibs;
}

int cls_map_proc(int pid) {
#ifdef __freestanding__
    int i;
    const struct bootinfo *boot = bootinfo_get();
    if (pid <= 0 || !boot || boot->hhdm_offset == 0)
        return 0;
    for (i = 0; i < CLS_MAX_LIBS; ++i) {
        ClsLoaded *L = &g_libs[i];
        uint64_t base;
        uint64_t phys;
        uint32_t span;
        uint32_t pages;
        uint32_t p;
        uint64_t data_va;
        uint64_t data_phys;
        uint8_t *dst;
        uint32_t ncopy;
        uint32_t b;
        if (!L->used || !L->code_buf)
            continue;
        base = (uint64_t)(uintptr_t)L->code_buf & ~4095ull;
        if (base < boot->hhdm_offset)
            continue;
        phys = base - boot->hhdm_offset;
        span = (uint32_t)((uintptr_t)L->code_buf & 4095u) + L->img.code_size;
        pages = (span + 4095u) / 4096u;
        if (pages > 8u)
            pages = 8u;
        for (p = 0; p < pages; ++p) {
            proc_map_user(pid, PROC_LIB_VIRT + (uint64_t)i * 0x100000ull +
                                   (uint64_t)p * 4096ull,
                          phys + (uint64_t)p * 4096ull, MM_PRESENT);
        }
        data_va = PROC_LIB_VIRT + 0x800000ull + (uint64_t)i * 4096ull;
        if (proc_commit(pid, data_va) != 0)
            continue;
        data_phys = proc_page_phys(pid, data_va);
        if (data_phys == 0 || !L->data_buf)
            continue;
        dst = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(data_phys);
        ncopy = L->img.data_size;
        if (ncopy > 4096u)
            ncopy = 4096u;
        for (b = 0; b < ncopy; ++b)
            dst[b] = L->data_buf[b];
    }
    return 1;
#else
    (void)pid;
    return 0;
#endif
}
