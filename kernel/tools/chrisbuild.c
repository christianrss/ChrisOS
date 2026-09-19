/* LEARN:SH16-19 */
#include "chrisbuild.h"
#include "fs.h"
#include "cfs.h"
#include "heap.h"
#include "serial.h"
#include "kcc.h"
#include "chrisld.h"
#include "chriso.h"
#include <stdint.h>
#include <string.h>

#define BUILD_MK "SYS/BUILD.MK"
#define BUILD_MAX_OBJ 48
#define BUILD_SRC_MAX 65536
#define BUILD_ELF_MAX CHRISLD_ELF_MAX

typedef struct {
    char kernel_out[FS_PATH];
    uint64_t kernel_ld;
    char c_paths[BUILD_MAX_OBJ][FS_PATH];
    int nc;
} BuildManifest;

static void trim(char *s) {
    int n = 0;
    int i;
    while (s[n]) {
        n++;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\\')) {
        s[--n] = 0;
    }
    i = 0;
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (i > 0) {
        int j = 0;
        while (s[i]) {
            s[j++] = s[i++];
        }
        s[j] = 0;
    }
}

static int parse_hex64(const char *s, uint64_t *out) {
    uint64_t v = 0;
    int i = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
    }
    for (; s[i]; i++) {
        int d;
        char c = s[i];
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            d = 10 + c - 'A';
        } else {
            return -1;
        }
        v = (v << 4) + (uint64_t)d;
    }
    *out = v;
    return 0;
}

static int manifest_add_c(BuildManifest *m, const char *path) {
    if (m->nc >= BUILD_MAX_OBJ) {
        return -1;
    }
    strncpy(m->c_paths[m->nc], path, FS_PATH - 1u);
    m->c_paths[m->nc][FS_PATH - 1u] = 0;
    m->nc++;
    return 0;
}

static int build_parse_mk(const char *text, BuildManifest *m) {
    char line[FS_PATH];
    int li = 0;
    int in_c = 0;
    const char *p = text;

    memset(m, 0, sizeof(*m));
    strncpy(m->kernel_out, "BIN/KERNEL.ELF", FS_PATH - 1u);
    m->kernel_ld = 0xffffffff80000000ull;

    while (*p) {
        char c = *p++;
        if (c != '\n' && li < (int)sizeof(line) - 1) {
            line[li++] = c;
            continue;
        }
        line[li] = 0;
        li = 0;
        trim(line);
        if (line[0] == 0 || line[0] == '#') {
            continue;
        }
        if (strncmp(line, "KERNEL_OUT=", 11) == 0) {
            strncpy(m->kernel_out, line + 11, FS_PATH - 1u);
            m->kernel_out[FS_PATH - 1u] = 0;
            trim(m->kernel_out);
            continue;
        }
        if (strncmp(line, "KERNEL_LD=", 10) == 0) {
            parse_hex64(line + 10, &m->kernel_ld);
            continue;
        }
        if (strncmp(line, "C_OBJECTS=", 10) == 0) {
            in_c = 1;
            if (line[10]) {
                char *tok = line + 10;
                trim(tok);
                if (tok[0]) {
                    manifest_add_c(m, tok);
                }
            }
            continue;
        }
        if (strncmp(line, "ASM_OBJECTS=", 12) == 0) {
            in_c = 0;
            continue;
        }
        if (in_c && line[0]) {
            manifest_add_c(m, line);
        }
    }
    return m->nc > 0 ? 0 : -1;
}

static int path_to_cfs(const char *hostish, char *out, int cap) {
    int o = 0;
    const char *p = hostish;

    if (strncmp(p, "SYS/", 4) == 0) {
        strncpy(out, p, (size_t)(cap - 1));
        out[cap - 1] = 0;
        return 0;
    }
    if (strncmp(p, "kernel/", 7) == 0) {
        p += 7;
    }
    if (o + 4 < cap) {
        out[o++] = 'S';
        out[o++] = 'Y';
        out[o++] = 'S';
        out[o++] = '/';
    }
    if (strncmp(p, "KERNEL/", 7) != 0) {
        if (o + 7 < cap) {
            out[o++] = 'K';
            out[o++] = 'E';
            out[o++] = 'R';
            out[o++] = 'N';
            out[o++] = 'E';
            out[o++] = 'L';
            out[o++] = '/';
        }
    }
    while (*p && o < cap - 1) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c == '/') {
            out[o++] = '/';
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
    return 0;
}

int chrisbuild_mk_kernel(void) {
    BuildManifest man;
    ChrisoImage merged;
    ChrisoImage unit;
    char *src_buf;
    uint8_t *text_buf;
    uint8_t *elf_buf;
    int i;
    int n;
    int elf_len;
    uint64_t entry;

    src_buf = (char *)kmalloc(BUILD_SRC_MAX);
    text_buf = (uint8_t *)kmalloc(65536u);
    elf_buf = (uint8_t *)kmalloc(BUILD_ELF_MAX);
    if (!src_buf || !text_buf || !elf_buf) {
        serial_puts("mk: nomem\n");
        if (src_buf) {
            kfree(src_buf);
        }
        if (text_buf) {
            kfree(text_buf);
        }
        if (elf_buf) {
            kfree(elf_buf);
        }
        return -1;
    }

    n = fs_read(BUILD_MK, src_buf, BUILD_SRC_MAX - 1);
    if (n < 0) {
        serial_puts("mk: no BUILD.MK\n");
        goto fail;
    }
    src_buf[n] = 0;
    if (build_parse_mk(src_buf, &man) != 0) {
        serial_puts("mk: bad manifest\n");
        goto fail;
    }
    chriso_init(&merged);
    merged.sec[CHRISO_SEC_TEXT] = text_buf;
    merged.sec_size[CHRISO_SEC_TEXT] = 0;

    for (i = 0; i < man.nc; i++) {
        char cfs_path[FS_PATH];
        path_to_cfs(man.c_paths[i], cfs_path, FS_PATH);
        serial_puts("mk: kcc ");
        serial_puts(cfs_path);
        serial_puts("\n");
        n = fs_read(cfs_path, src_buf, BUILD_SRC_MAX - 1);
        if (n < 0) {
            serial_puts("mk: read fail\n");
            goto fail;
        }
        src_buf[n] = 0;
        if (kcc_compile_source(src_buf, &unit) != 0) {
            serial_puts("mk: kcc fail\n");
            goto fail;
        }
        if (chriso_merge_text(&merged, &unit) != 0) {
            serial_puts("mk: merge fail\n");
            goto fail;
        }
        if (unit.sec[CHRISO_SEC_TEXT]) {
            kfree(unit.sec[CHRISO_SEC_TEXT]);
            unit.sec[CHRISO_SEC_TEXT] = 0;
        }
    }
    elf_len = chrisld_link(&merged, man.kernel_ld, elf_buf, BUILD_ELF_MAX, &entry);
    if (elf_len < 0) {
        serial_puts("mk: link fail\n");
        goto fail;
    }
    if (fs_write(man.kernel_out, elf_buf, elf_len) != elf_len) {
        serial_puts("mk: write fail\n");
        goto fail;
    }
    serial_puts("mk: kernel ok bytes=");
    serial_write_u64((uint64_t)elf_len);
    serial_puts("\n");
    kfree(src_buf);
    kfree(text_buf);
    kfree(elf_buf);
    return 0;

fail:
    kfree(src_buf);
    kfree(text_buf);
    kfree(elf_buf);
    return -1;
}

int chrisbuild_mk_clean(void) {
    if (fs_unlink("BIN/KERNEL.ELF") != CFS_OK) {
        serial_puts("mk: clean (no file)\n");
    } else {
        serial_puts("mk: clean done\n");
    }
    return 0;
}

int chrisbuild_mk_install(void) {
    uint8_t *elf_buf;
    int n;

    elf_buf = (uint8_t *)kmalloc(BUILD_ELF_MAX);
    if (!elf_buf) {
        serial_puts("mk: install nomem\n");
        return -1;
    }
    n = fs_read("BIN/KERNEL.ELF", elf_buf, BUILD_ELF_MAX);
    if (n < 0) {
        serial_puts("mk: install no elf\n");
        kfree(elf_buf);
        return -1;
    }
    if (fs_write("BIN/KERNEL.ELF", elf_buf, n) != n) {
        serial_puts("mk: install fail\n");
        kfree(elf_buf);
        return -1;
    }
    kfree(elf_buf);
    serial_puts("mk: install ok\n");
    return 0;
}
