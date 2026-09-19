/* LEARN:CFS64-MKDISK — formata disk.img CFS v2 com pastas seed */
#include "cfs.h"
#include "cfs_format.h"
#include "storage_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_f;

static int fread_dev(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    (void)ctx;
    if (fseek(g_f, (long)lba * 512, SEEK_SET) != 0) {
        return BD_EIO;
    }
    if (fread(dst, 512, count, g_f) != count) {
        return BD_EIO;
    }
    return BD_OK;
}

static int fwrite_dev(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    (void)ctx;
    if (fseek(g_f, (long)lba * 512, SEEK_SET) != 0) {
        return BD_EIO;
    }
    if (fwrite(src, 512, count, g_f) != count) {
        return BD_EIO;
    }
    return BD_OK;
}

static int fflush_dev(void *ctx) {
    (void)ctx;
    return fflush(g_f) == 0 ? BD_OK : BD_EIO;
}

static int seed_dir(Cfs *fs, const char *name) {
    int rc = cfs_mkdir(fs, name);
    if (rc == CFS_OK || rc == CFS_EEXIST) {
        return CFS_OK;
    }
    return rc;
}

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    const char *path;
    int rc;

    path = (argc >= 2) ? argv[1] : "disk.img";
    g_f = fopen(path, "w+b");
    if (!g_f) {
        fprintf(stderr, "cannot create %s\n", path);
        return 1;
    }
    if (fseek(g_f, (long)STOR_DISK_BYTES - 1, SEEK_SET) != 0 ||
        fputc(0, g_f) == EOF) {
        fprintf(stderr, "cannot size %s to %u bytes\n", path,
                (unsigned)STOR_DISK_BYTES);
        fclose(g_f);
        return 2;
    }
    memset(&d, 0, sizeof(d));
    d.sector_size = STOR_SECTOR_SIZE;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = fread_dev;
    d.write = fwrite_dev;
    d.flush = fflush_dev;
    d.writable = 1u;
    rc = cfs_format(&d);
    if (rc != CFS_OK) {
        fprintf(stderr, "cfs_format failed (%d)\n", rc);
        fclose(g_f);
        return 3;
    }
    rc = cfs_mount(&fs, &d);
    if (rc != CFS_OK) {
        fprintf(stderr, "cfs_mount failed (%d)\n", rc);
        fclose(g_f);
        return 4;
    }
    rc = seed_dir(&fs, "GAMES");
    if (rc != CFS_OK) {
        goto fail;
    }
    rc = seed_dir(&fs, "SRC");
    if (rc != CFS_OK) {
        goto fail;
    }
    rc = seed_dir(&fs, "BIN");
    if (rc != CFS_OK) {
        goto fail;
    }
    rc = cfs_sync(&fs);
    if (rc != CFS_OK) {
        goto fail;
    }
    fclose(g_f);
    printf("cfs_mkdisk: %s (%u MiB CFS v%u, GAMES SRC BIN)\n", path,
           (unsigned)(STOR_DISK_BYTES / (1024u * 1024u)),
           (unsigned)CFS_VERSION);
    return 0;

fail:
    fprintf(stderr, "seed failed (%d)\n", rc);
    fclose(g_f);
    return 5;
}
