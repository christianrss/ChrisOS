#include "native_link.h"
#include "chrisld.h"
#include "fs.h"
#include "heap.h"
#include <stdint.h>
#include <string.h>

static int path_prefix(const char *path, const char *pfx) {
    while (*pfx) {
        if (*path++ != *pfx++) {
            return 0;
        }
    }
    return 1;
}

void native_elf_path(const char *src_path, char *out_path, int out_cap) {
    const char *base;
    int i;
    int slash = -1;
    int dot = -1;
    int j = 0;

    if (!src_path || !out_path || out_cap < 8) {
        if (out_path && out_cap > 0) {
            out_path[0] = 0;
        }
        return;
    }
    for (i = 0; src_path[i]; i++) {
        if (src_path[i] == '/') {
            slash = i;
        }
    }
    base = slash >= 0 ? src_path + slash + 1 : src_path;
    if (path_prefix(src_path, "SRC/")) {
        if (j + 4 < out_cap) {
            out_path[j++] = 'B';
            out_path[j++] = 'I';
            out_path[j++] = 'N';
            out_path[j++] = '/';
        }
    } else if (slash >= 0) {
        for (i = 0; i <= slash && j < out_cap - 1; i++) {
            out_path[j++] = src_path[i];
        }
    }
    for (i = 0; base[i] && j < out_cap - 1; i++) {
        if (base[i] == '.') {
            dot = j;
            break;
        }
        out_path[j++] = base[i];
    }
    if (dot >= 0) {
        j = dot;
    }
    if (j + 4 < out_cap) {
        out_path[j++] = '.';
        out_path[j++] = 'E';
        out_path[j++] = 'L';
        out_path[j++] = 'F';
    }
    out_path[j < out_cap ? j : out_cap - 1] = 0;
}

void native_image_free(ChrisoImage *img) {
    if (!img) {
        return;
    }
    if (img->sec[CHRISO_SEC_TEXT]) {
        kfree(img->sec[CHRISO_SEC_TEXT]);
        img->sec[CHRISO_SEC_TEXT] = 0;
        img->sec_size[CHRISO_SEC_TEXT] = 0;
    }
}

int native_link_write_elf(const ChrisoImage *img, const char *out_path) {
    uint8_t *elf;
    uint64_t entry;
    int n;

    if (!img || !out_path || !out_path[0]) {
        return -1;
    }
    elf = (uint8_t *)kmalloc(CHRISLD_ELF_MAX);
    if (!elf) {
        return -1;
    }
    n = chrisld_link(img, NATIVE_USER_LOAD, elf, CHRISLD_ELF_MAX, &entry);
    if (n < 0) {
        kfree(elf);
        return -1;
    }
    if (fs_write(out_path, elf, n) != n) {
        kfree(elf);
        return -1;
    }
    kfree(elf);
    return n;
}
