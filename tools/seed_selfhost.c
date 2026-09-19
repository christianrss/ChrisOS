/* LEARN:SH16-22 — seed SYS/ + BIN/KCC.ELF on CFS disk */
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

static int read_host(const char *path, unsigned char **out, size_t *out_len) {
    FILE *hf = fopen(path, "rb");
    long sz;
    unsigned char *buf;

    if (!hf) {
        return -1;
    }
    if (fseek(hf, 0, SEEK_END) != 0) {
        fclose(hf);
        return -1;
    }
    sz = ftell(hf);
    if (sz < 0) {
        fclose(hf);
        return -1;
    }
    if (fseek(hf, 0, SEEK_SET) != 0) {
        fclose(hf);
        return -1;
    }
    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fclose(hf);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, hf) != (size_t)sz) {
        free(buf);
        fclose(hf);
        return -1;
    }
    fclose(hf);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

static int put_file(Cfs *fs, const char *cfs_path, const char *host_path) {
    unsigned char *buf = 0;
    size_t len = 0;
    int rc;

    if (read_host(host_path, &buf, &len) != 0) {
        fprintf(stderr, "seed: cannot read %s\n", host_path);
        return -1;
    }
    rc = cfs_write(fs, cfs_path, buf, (uint32_t)len);
    free(buf);
    if (rc < 0 || rc != (int)len) {
        fprintf(stderr, "seed: cfs_write %s rc=%d\n", cfs_path, rc);
        return -1;
    }
    return 0;
}

static int ensure_dir(Cfs *fs, const char *name) {
    int rc = cfs_mkdir(fs, name);
    if (rc == CFS_OK || rc == CFS_EEXIST) {
        return CFS_OK;
    }
    return rc;
}

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    const char *disk_path;
    int rc;

    disk_path = (argc >= 2) ? argv[1] : "disk.img";
    g_f = fopen(disk_path, "r+b");
    if (!g_f) {
        fprintf(stderr, "seed: cannot open %s\n", disk_path);
        return 1;
    }
    memset(&d, 0, sizeof(d));
    d.sector_size = STOR_SECTOR_SIZE;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = fread_dev;
    d.write = fwrite_dev;
    d.flush = fflush_dev;
    d.writable = 1u;
    rc = cfs_mount(&fs, &d);
    if (rc != CFS_OK) {
        fprintf(stderr, "seed: mount failed %d\n", rc);
        fclose(g_f);
        return 2;
    }
    if (ensure_dir(&fs, "SYS") != CFS_OK ||
        ensure_dir(&fs, "SYS/KERNEL") != CFS_OK ||
        ensure_dir(&fs, "SYS/KERNEL/METAL") != CFS_OK ||
        ensure_dir(&fs, "SYS/TOOLS") != CFS_OK ||
        ensure_dir(&fs, "BIN") != CFS_OK) {
        fprintf(stderr, "seed: mkdir failed\n");
        fclose(g_f);
        return 3;
    }
    if (put_file(&fs, "SYS/BUILD.MK",
                 "learn/fase16-selfhost64/stubs/SYS_BUILD.MK") != 0 ||
        put_file(&fs, "SYS/KERNEL/METAL/START.C", "kernel/metal/start.c") != 0 ||
        put_file(&fs, "SYS/KERNEL/METAL/SERIAL.C", "kernel/metal/serial.c") != 0 ||
        put_file(&fs, "SYS/KERNEL/METAL/PORT.C", "kernel/metal/port.c") != 0 ||
        put_file(&fs, "SYS/KERNEL/METAL/SERIAL.H", "kernel/metal/serial.h") != 0 ||
        put_file(&fs, "SYS/KERNEL/METAL/PORT.H", "kernel/metal/port.h") != 0) {
        fclose(g_f);
        return 4;
    }
    if (put_file(&fs, "BIN/KCC.ELF", "build/host/kcc") != 0) {
        fprintf(stderr, "seed: run make host-kcc first\n");
        fclose(g_f);
        return 5;
    }
    rc = cfs_sync(&fs);
    fclose(g_f);
    if (rc != CFS_OK) {
        fprintf(stderr, "seed: sync failed %d\n", rc);
        return 6;
    }
    printf("seed_selfhost: ok on %s\n", disk_path);
    return 0;
}
