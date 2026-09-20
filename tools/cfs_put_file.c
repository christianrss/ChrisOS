/* LEARN:USER64-05 */
#include "cfs.h"
#include "cfs_format.h"
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

static int read_host_file(const char *path, unsigned char **out, size_t *out_len) {
    FILE *hf;
    long sz;
    unsigned char *buf;

    hf = fopen(path, "rb");
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

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    unsigned char *data = 0;
    size_t data_len = 0;
    int n;

    if (argc < 4) {
        fprintf(stderr, "usage: %s disk.img CFS/PATH host-file\n", argv[0]);
        return 1;
    }
    g_f = fopen(argv[1], "r+b");
    if (!g_f) {
        fprintf(stderr, "cannot open %s (QEMU rodando? use make run-stop)\n", argv[1]);
        return 2;
    }
    if (fseek(g_f, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot seek %s\n", argv[1]);
        fclose(g_f);
        return 2;
    }
    {
        long img_bytes = ftell(g_f);
        if (img_bytes < (long)STOR_DISK_BYTES) {
            fprintf(stderr,
                    "%s tem %ld bytes; precisa %u (512 MiB CFS v3).\n"
                    "Rode: make disk.img   ou   make seed-selfhost\n",
                    argv[1], img_bytes, (unsigned)STOR_DISK_BYTES);
            fclose(g_f);
            return 2;
        }
        fseek(g_f, 0, SEEK_SET);
    }
    if (read_host_file(argv[3], &data, &data_len) != 0) {
        fprintf(stderr, "cannot read %s\n", argv[3]);
        fclose(g_f);
        return 3;
    }
    memset(&d, 0, sizeof(d));
    d.sector_size = 512;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = fread_dev;
    d.write = fwrite_dev;
    d.flush = fflush_dev;
    d.writable = 1;
    {
        int mrc = cfs_mount(&fs, &d);
        if (mrc != CFS_OK) {
            fprintf(stderr, "cfs mount failed (%d)\n", mrc);
            if (mrc == CFS_EFORMAT) {
                fprintf(stderr,
                        "disco sem CFS v3 valido. Rode: make disk.img\n"
                        "se era v2 (16 MiB): tools/cfs_migrate_v2v3\n");
            }
            free(data);
            fclose(g_f);
            return 4;
        }
    }
    {
        char acc[512];
        int i;
        int nacc;
        nacc = 0;
        for (i = 0; argv[2][i]; ++i) {
            if (argv[2][i] == '/' && nacc > 0) {
                int mrc;
                acc[nacc] = 0;
                mrc = cfs_mkdir(&fs, acc);
                if (mrc != CFS_OK && mrc != CFS_EEXIST) {
                    fprintf(stderr, "cfs_mkdir %s failed (%d)\n", acc, mrc);
                    free(data);
                    fclose(g_f);
                    return 5;
                }
            }
            if (nacc + 1 < 512) {
                acc[nacc++] = argv[2][i];
            }
        }
    }
    n = cfs_write(&fs, argv[2], data, (uint32_t)data_len);
    if (n != (int)data_len) {
        fprintf(stderr, "cfs_write failed (%d)", n);
        if (n == CFS_ENOENT) {
            fprintf(stderr, " — pasta pai inexistente (ex.: use GAMES/ ou make seed-selfhost)\n");
        } else {
            fprintf(stderr, "\n");
        }
        free(data);
        fclose(g_f);
        return 5;
    }
    if (cfs_sync(&fs) != CFS_OK) {
        fprintf(stderr, "cfs_sync failed\n");
        free(data);
        fclose(g_f);
        return 6;
    }
    free(data);
    fclose(g_f);
    printf("cfs_put_file: %s -> %s (%zu bytes)\n", argv[3], argv[2], data_len);
    return 0;
}
