/* LEARN:SH16-06 — copie para tools/cfs_migrate_v2v3.c */
#include "cfs.h"
#include "cfs_format.h"
#include <stdio.h>
#include <string.h>

#define V2_SECTORS         32768u
#define V2_INODE_LBA       9u
#define V2_INODE_SIZE       128u
#define V2_DIRECT           12u
#define V2_DIRENT           32u
#define V2_NAME_MAX         24u

static FILE *g_src;
static FILE *g_dst;

static int src_read(uint32_t lba, uint8_t dst[512]) {
    if (fseek(g_src, (long)lba * 512, SEEK_SET) != 0) return -1;
    return fread(dst, 512, 1, g_src) == 1 ? 0 : -1;
}

static int dst_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    (void)ctx;
    if (fseek(g_dst, (long)lba * 512, SEEK_SET) != 0) return BD_EIO;
    return fread(dst, 512, count, g_dst) == count ? BD_OK : BD_EIO;
}

static int dst_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    (void)ctx;
    if (fseek(g_dst, (long)lba * 512, SEEK_SET) != 0) return BD_EIO;
    return fwrite(src, 512, count, g_dst) == count ? BD_OK : BD_EIO;
}

static int dst_flush(void *ctx) {
    (void)ctx;
    return fflush(g_dst) == 0 ? BD_OK : BD_EIO;
}

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static int v2_inode(uint32_t id, uint8_t out[128]) {
    uint8_t sec[512];
    uint32_t per = 512u / V2_INODE_SIZE;
    if (src_read(V2_INODE_LBA + id / per, sec) != 0) return -1;
    memcpy(out, sec + (id % per) * V2_INODE_SIZE, V2_INODE_SIZE);
    return 0;
}

static int copy_file(Cfs *dst, const char *path, uint32_t ino) {
    uint8_t raw[128];
    uint8_t data[1024 * 1024];
    uint32_t size, b, take, done = 0, lba;
    uint16_t type;
    if (v2_inode(ino, raw) != 0) return -1;
    type = u16(raw + 0);
    size = u32(raw + 4);
    if (type != CFS_INODE_FILE) return 0;
    if (size > CFS_MAX_FILE_SIZE) size = CFS_MAX_FILE_SIZE;
    memset(data, 0, sizeof(data));
    for (b = 0; done < size && b < V2_DIRECT; b++) {
        uint8_t sec[512];
        lba = u32(raw + 12 + b * 4);
        if (!lba) break;
        if (src_read(lba, sec) != 0) return -1;
        take = size - done;
        if (take > 512u) take = 512u;
        memcpy(data + done, sec, take);
        done += take;
    }
    return cfs_write(dst, path, data, size) == (int)size ? 0 : -1;
}

static int walk(Cfs *dst, uint32_t dir_ino, const char *prefix);

static int walk(Cfs *dst, uint32_t dir_ino, const char *prefix) {
    uint8_t raw[128];
    uint32_t b, slot;
    if (v2_inode(dir_ino, raw) != 0) return -1;
    if (u16(raw + 0) != 2) return 0;
    for (b = 0; b < V2_DIRECT; b++) {
        uint32_t lba = u32(raw + 12 + b * 4);
        uint8_t sec[512];
        if (!lba) continue;
        if (src_read(lba, sec) != 0) return -1;
        for (slot = 0; slot < 16u; slot++) {
            uint8_t *e = sec + slot * V2_DIRENT;
            uint32_t ino = u32(e);
            uint8_t nlen = e[5];
            uint8_t type = e[4];
            char name[32];
            char path[CFS_PATH_MAX];
            uint32_t i;
            if (!ino || !nlen || nlen > V2_NAME_MAX) continue;
            for (i = 0; i < nlen; i++) name[i] = (char)e[8 + i];
            name[nlen] = 0;
            if (prefix[0]) snprintf(path, sizeof(path), "%s/%s", prefix, name);
            else snprintf(path, sizeof(path), "%s", name);
            if (type == 2) {
                if (cfs_mkdir(dst, path) != CFS_OK &&
                    cfs_mkdir(dst, path) != CFS_EEXIST) return -1;
                if (walk(dst, ino, path) != 0) return -1;
            } else {
                if (copy_file(dst, path, ino) != 0) return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    BlockDevice d;
    Cfs fs;
    uint8_t super[512];
    if (argc < 3) {
        fprintf(stderr, "usage: cfs_migrate_v2v3 src.img dst.img\n");
        return 1;
    }
    g_src = fopen(argv[1], "rb");
    g_dst = fopen(argv[2], "w+b");
    if (!g_src || !g_dst) return 2;
    if (src_read(0, super) != 0) return 3;
    if (u32(super) != 0x31534643u || u16(super + 4) != 2u) {
        fprintf(stderr, "src is not CFS v2\n");
        return 4;
    }
    if (fseek(g_dst, (long)STOR_DISK_BYTES - 1, SEEK_SET) != 0) return 5;
    if (fputc(0, g_dst) == EOF) return 5;
    memset(&d, 0, sizeof(d));
    d.sector_size = 512;
    d.sector_count = STOR_DISK_SECTORS;
    d.read = dst_read;
    d.write = dst_write;
    d.flush = dst_flush;
    d.writable = 1;
    if (cfs_format(&d) != CFS_OK) return 6;
    if (cfs_mount(&fs, &d) != CFS_OK) return 7;
    if (walk(&fs, 0, "") != 0) return 8;
    if (cfs_sync(&fs) != CFS_OK) return 9;
    fclose(g_src);
    fclose(g_dst);
    puts("cfs_migrate_v2v3: ok");
    return 0;
}
