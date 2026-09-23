#include "install.h"

#include "bdev.h"
#include "cfs.h"
#include "fs.h"
#include "heap.h"
#include "part.h"
#include "serial.h"
#include "storage_limits.h"

static uint8_t g_sec[512];
static uint8_t g_chunk[2048];
static uint8_t g_entries[128 * 128];
static uint16_t g_fat[16384];
static Cfs g_fs;

static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    int i;
    int b;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (b = 0; b < 8; ++b)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static void zero(uint8_t *p, int n) {
    int i;
    for (i = 0; i < n; ++i)
        p[i] = 0;
}

static uint32_t g_disk_limit;

static int put_sec(BlockDevice *d, uint32_t lba, const uint8_t *src) {
    if (!d || lba >= g_disk_limit || lba >= d->sector_count)
        return BD_ERANGE;
    return bd_write(d, lba, 1, src);
}

static void fat_name(uint8_t ent[32], const char *shortname, uint8_t attr,
                     uint16_t cluster, uint32_t size) {
    int i;
    zero(ent, 32);
    for (i = 0; i < 11; ++i)
        ent[i] = (uint8_t)shortname[i];
    ent[11] = attr;
    ent[26] = (uint8_t)cluster;
    ent[27] = (uint8_t)(cluster >> 8);
    wr32(ent + 28, size);
}

static int esp_put_cluster(BlockDevice *disk, uint32_t esp_lba, uint32_t data,
                           uint32_t cluster, const uint8_t *bytes) {
    uint32_t s;
    for (s = 0; s < 4u; ++s) {
        if (put_sec(disk, esp_lba + data + (cluster - 2u) * 4u + s,
                    bytes + s * 512u) != BD_OK)
            return -1;
    }
    return 0;
}

static int esp_put_file(BlockDevice *disk, uint32_t esp_lba, uint32_t data,
                        const char *path, uint32_t *cluster, uint8_t ent[32]) {
    uint32_t size = 0;
    uint16_t type = 0;
    uint32_t off = 0;
    uint32_t first;
    uint32_t slots;
    const char *name = "LIMINE  CFG";
    if (path[0] == 'E')
        name = "BOOTX64 EFI";
    if (path[5] == 'K')
        name = "KERNEL  ELF";
    if (fs_stat(path, &size, &type) != 0 || size == 0 ||
        size > 8u * 1024u * 1024u)
        return -1;
    slots = (size + 2047u) / 2048u;
    first = *cluster;
    if (first + slots >= 16000u)
        return -1;
    while (off < size) {
        int n = fs_read_at(path, off, g_chunk, 2048);
        uint32_t s;
        if (n <= 0)
            return -1;
        for (s = (uint32_t)n; s < 2048u; ++s)
            g_chunk[s] = 0;
        if (esp_put_cluster(disk, esp_lba, data, *cluster, g_chunk) != 0)
            return -1;
        g_fat[*cluster] =
            (off + 2048u >= size) ? 0xFFFFu : (uint16_t)(*cluster + 1u);
        *cluster += 1u;
        off += (uint32_t)n;
    }
    fat_name(ent, name, 0x20, (uint16_t)first, size);
    (void)slots;
    return 0;
}

static void fat_lfn(uint8_t *ent, const char *longname, const char *short11) {
    uint8_t sum = 0;
    int i;
    int o;
    for (i = 0; i < 32; ++i)
        ent[i] = 0xFF;
    for (i = 0; i < 11; ++i)
        sum = (uint8_t)(((sum & 1u) << 7) + (sum >> 1) + (uint8_t)short11[i]);
    ent[0] = 0x41;
    ent[11] = 0x0F;
    ent[12] = 0;
    ent[13] = sum;
    ent[26] = 0;
    ent[27] = 0;
    for (i = 0; i < 13; ++i) {
        unsigned char c = (unsigned char)longname[i];
        if (i < 5)
            o = 1 + i * 2;
        else if (i < 11)
            o = 14 + (i - 5) * 2;
        else
            o = 28 + (i - 11) * 2;
        ent[o] = c;
        ent[o + 1] = 0;
        if (c == 0)
            break;
    }
}

static int write_esp(BlockDevice *disk, uint32_t esp_lba, uint32_t esp_secs,
                     uint32_t spf) {
    uint32_t i;
    (void)esp_secs;
    uint32_t root = 1u + spf * 2u;
    uint32_t data = root + 32u;
    uint8_t ent[32];
    uint8_t dir[2048];
    uint32_t file_cluster;
    zero(g_sec, 512);
    g_sec[0] = 0xEB;
    g_sec[1] = 0x3C;
    g_sec[2] = 0x90;
    for (i = 0; i < 8; ++i)
        g_sec[3 + i] = (uint8_t)"CHRISOS "[i];
    g_sec[11] = 0x00;
    g_sec[12] = 0x02;
    g_sec[13] = 4;
    g_sec[14] = 1;
    g_sec[16] = 2;
    g_sec[17] = 0x00;
    g_sec[18] = 0x02;
    g_sec[21] = 0xF8;
    g_sec[22] = (uint8_t)spf;
    g_sec[23] = (uint8_t)(spf >> 8);
    g_sec[24] = 63;
    g_sec[26] = 255;
    wr32(g_sec + 28, esp_lba);
    wr32(g_sec + 32, esp_secs);
    g_sec[36] = 0x80;
    g_sec[38] = 0x29;
    wr32(g_sec + 39, 0x43524F53u);
    for (i = 0; i < 11; ++i)
        g_sec[43 + i] = (uint8_t)"CHRISOS ESP"[i];
    g_sec[54] = 'F';
    g_sec[55] = 'A';
    g_sec[56] = 'T';
    g_sec[57] = '1';
    g_sec[58] = '6';
    g_sec[510] = 0x55;
    g_sec[511] = 0xAA;
    if (put_sec(disk, esp_lba, g_sec) != BD_OK)
        return -1;
    for (i = 0; i < 16384u; ++i)
        g_fat[i] = 0;
    g_fat[0] = 0xFFF8;
    g_fat[1] = 0xFFFF;
    g_fat[2] = 0xFFFF;
    g_fat[3] = 0xFFFF;
    g_fat[4] = 0xFFFF;
    g_fat[5] = 0xFFFF;
    file_cluster = 6u;
    if (esp_put_file(disk, esp_lba, data, "EFI/BOOT/BOOTX64.EFI", &file_cluster,
                     ent) != 0)
        return -1;
    zero(dir, 2048);
    fat_name(dir, ".          ", 0x10, 3, 0);
    fat_name(dir + 32, "..         ", 0x10, 0, 0);
    for (i = 0; i < 32; ++i)
        dir[64 + i] = ent[i];
    if (esp_put_cluster(disk, esp_lba, data, 3, dir) != 0)
        return -1;
    zero(dir, 2048);
    fat_name(dir, ".          ", 0x10, 2, 0);
    fat_name(dir + 32, "..         ", 0x10, 0, 0);
    fat_name(dir + 64, "BOOT       ", 0x10, 3, 0);
    if (esp_put_cluster(disk, esp_lba, data, 2, dir) != 0)
        return -1;
    if (esp_put_file(disk, esp_lba, data, "BOOT/KERNEL.ELF", &file_cluster,
                     ent) != 0)
        return -1;
    zero(dir, 2048);
    fat_name(dir, ".          ", 0x10, 4, 0);
    fat_name(dir + 32, "..         ", 0x10, 0, 0);
    fat_name(dir + 64, "LIMINE     ", 0x10, 5, 0);
    for (i = 0; i < 32; ++i)
        dir[96 + i] = ent[i];
    if (esp_put_cluster(disk, esp_lba, data, 4, dir) != 0)
        return -1;
    if (esp_put_file(disk, esp_lba, data, "BOOT/LIMINE.CFG", &file_cluster,
                     ent) != 0)
        return -1;
    zero(dir, 2048);
    fat_name(dir, ".          ", 0x10, 5, 0);
    fat_name(dir + 32, "..         ", 0x10, 4, 0);
    fat_lfn(dir + 64, "limine.conf", "LIMINE  CFG");
    for (i = 0; i < 32; ++i)
        dir[96 + i] = ent[i];
    if (esp_put_cluster(disk, esp_lba, data, 5, dir) != 0)
        return -1;
    zero(g_sec, 512);
    fat_name(g_sec, "EFI        ", 0x10, 2, 0);
    fat_name(g_sec + 32, "BOOT       ", 0x10, 4, 0);
    if (put_sec(disk, esp_lba + root, g_sec) != BD_OK)
        return -1;
    for (i = 0; i < spf; ++i) {
        uint32_t e;
        zero(g_sec, 512);
        for (e = 0; e < 256u; ++e) {
            uint32_t idx = i * 256u + e;
            g_sec[e * 2u] = (uint8_t)g_fat[idx];
            g_sec[e * 2u + 1u] = (uint8_t)(g_fat[idx] >> 8);
        }
        if (put_sec(disk, esp_lba + 1u + i, g_sec) != BD_OK)
            return -1;
        if (put_sec(disk, esp_lba + 1u + spf + i, g_sec) != BD_OK)
            return -1;
    }
    return 0;
}

static int require_boot_files(void) {
    static const char *paths[] = {"BOOT/KERNEL.ELF", "EFI/BOOT/BOOTX64.EFI",
                                  "BOOT/LIMINE.CFG"};
    int i;
    for (i = 0; i < 3; ++i) {
        uint32_t size = 0;
        uint16_t type = 0;
        if (fs_stat(paths[i], &size, &type) != 0 || size == 0 ||
            size > 8u * 1024u * 1024u) {
            serial_puts("INSTALL FAIL: missing ");
            serial_puts(paths[i]);
            serial_puts("\n");
            return -1;
        }
    }
    return 0;
}

static int write_gpt_backup(BlockDevice *disk, uint32_t sectors) {
    uint32_t i;
    uint8_t hdr[512];
    uint32_t crc;
    if (sectors < 67u)
        return -1;
    for (i = 0; i < 32u; ++i) {
        if (bd_read(disk, 2u + i, 1, g_sec) != BD_OK)
            return -1;
        if (put_sec(disk, sectors - 33u + i, g_sec) != BD_OK)
            return -1;
    }
    if (bd_read(disk, 1, 1, hdr) != BD_OK)
        return -1;
    wr64(hdr + 24, sectors - 1u);
    wr64(hdr + 32, 1u);
    wr64(hdr + 72, (uint64_t)sectors - 33u);
    wr32(hdr + 16, 0);
    crc = crc32(hdr, 92);
    wr32(hdr + 16, crc);
    if (put_sec(disk, sectors - 1u, hdr) != BD_OK)
        return -1;
    serial_puts("install backup gpt\n");
    return 0;
}

static int copy_file(Cfs *dst, const char *path, uint32_t size) {
    uint32_t off = 0;
    while (off < size) {
        int n = fs_read_at(path, off, g_chunk, 2048);
        if (n <= 0)
            return -1;
        if (cfs_write_at(dst, path, off, g_chunk, (uint32_t)n) < 0)
            return -1;
        off += (uint32_t)n;
    }
    return 0;
}

static int copy_dir(Cfs *dst, const char *path);

static int copy_ent(void *ctx, const char *name, uint32_t size, uint16_t type) {
    void **box = (void **)ctx;
    Cfs *dst = (Cfs *)box[0];
    const char *dir = (const char *)box[1];
    char child[160];
    int n = 0;
    int k = 0;
    if (!name || name[0] == '.')
        return 0;
    while (dir[n] && n + 1 < 140)
        child[n] = dir[n], n++;
    if (n > 0)
        child[n++] = '/';
    while (name[k] && n + 1 < 160)
        child[n++] = name[k++];
    child[n] = 0;
    if (type == 2) {
        cfs_mkdir(dst, child);
        return copy_dir(dst, child);
    }
    return copy_file(dst, child, size);
}

static int copy_dir(Cfs *dst, const char *path) {
    void *ctx[2];
    uint32_t size = 0;
    uint16_t type = 0;
    if (fs_stat(path, &size, &type) != 0)
        return 0;
    cfs_mkdir(dst, path);
    ctx[0] = dst;
    ctx[1] = (void *)path;
    {
        int rc = fs_list_at(path, copy_ent, ctx);
        /* A successful list returns the entry count, not zero. */
        return rc < 0 ? rc : 0;
    }
}

static int copy_tree(Cfs *dst) {
    static const char *dirs[] = {"SYS", "APPS", "LIB", "GAMES", "BOOT",
                                 "SRC", "BIN"};
    int i;
    for (i = 0; i < 7; ++i) {
        if (copy_dir(dst, dirs[i]) != 0)
            return -1;
    }
    return 0;
}

static int install_device(BlockDevice *disk, const char *label, int copy_os) {
    PartView part;
    uint32_t sectors;
    uint32_t esp = 16384u;
    uint32_t spf = 16u;
    uint32_t cfs_lba;
    uint32_t i;
    uint32_t entries_crc;
    uint32_t header_crc;
    uint8_t efi_guid[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                            0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    /* GUID 43524653-3100-4000-8000-000000000001 */
    uint8_t cfs_guid[16] = {0x53, 0x46, 0x52, 0x43, 0x00, 0x31, 0x00, 0x40,
                            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (!disk || !disk->writable)
        return -1;
    sectors = disk->sector_count;
    if (sectors > 200000u) {
        esp = 65536u;
        spf = 64u;
    }
    if (sectors < 2048u + esp + (uint32_t)STOR_DISK_SECTORS + 64u) {
        serial_puts("install too small\n");
        return -1;
    }
    if (require_boot_files() != 0)
        return -1;
    cfs_lba = 2048u + esp;
    g_disk_limit = sectors;
    zero(g_sec, 512);
    g_sec[446] = 0x00;
    g_sec[450] = 0xEE;
    wr32(g_sec + 454, 1u);
    wr32(g_sec + 458, sectors > 0xFFFFFFFFu ? 0xFFFFFFFFu : sectors - 1u);
    g_sec[510] = 0x55;
    g_sec[511] = 0xAA;
    if (put_sec(disk, 0, g_sec) != BD_OK)
        return -1;
    zero(g_entries, (int)sizeof(g_entries));
    for (i = 0; i < 16; ++i)
        g_entries[i] = efi_guid[i];
    g_entries[16] = 1;
    wr64(g_entries + 32, 2048u);
    wr64(g_entries + 40, 2048u + esp - 1u);
    for (i = 0; i < 16; ++i)
        g_entries[128 + i] = cfs_guid[i];
    g_entries[128 + 16] = 2;
    wr64(g_entries + 128 + 32, cfs_lba);
    wr64(g_entries + 128 + 40, sectors - 34u);
    for (i = 0; i < 32; ++i) {
        zero(g_sec, 512);
        for (header_crc = 0; header_crc < 512; ++header_crc)
            g_sec[header_crc] = g_entries[i * 512u + header_crc];
        if (put_sec(disk, 2u + i, g_sec) != BD_OK)
            return -1;
    }
    entries_crc = crc32(g_entries, 128 * 128);
    zero(g_sec, 512);
    g_sec[0] = 'E';
    g_sec[1] = 'F';
    g_sec[2] = 'I';
    g_sec[3] = ' ';
    g_sec[4] = 'P';
    g_sec[5] = 'A';
    g_sec[6] = 'R';
    g_sec[7] = 'T';
    wr32(g_sec + 8, 0x00010000u);
    wr32(g_sec + 12, 92u);
    wr64(g_sec + 24, 1u);
    wr64(g_sec + 32, sectors - 1u);
    wr64(g_sec + 40, 34u);
    wr64(g_sec + 48, sectors - 34u);
    g_sec[56] = 0x43;
    wr64(g_sec + 72, 2u);
    wr32(g_sec + 80, 128u);
    wr32(g_sec + 84, 128u);
    wr32(g_sec + 88, entries_crc);
    header_crc = crc32(g_sec, 92);
    wr32(g_sec + 16, header_crc);
    if (put_sec(disk, 1, g_sec) != BD_OK)
        return -1;
    if (write_gpt_backup(disk, sectors) != 0)
        return -1;
    if (write_esp(disk, 2048u, esp, spf) != 0)
        return -1;
    if (part_open(&part, disk, cfs_lba, sectors - cfs_lba - 34u) != 0)
        return -1;
    if (cfs_format(&part.dev) != 0)
        return -1;
    if (cfs_mount(&g_fs, &part.dev) != 0)
        return -1;
    if (copy_os) {
        if (copy_tree(&g_fs) != 0) {
            serial_puts("INSTALL FAIL: copy\n");
            return -1;
        }
        serial_puts("install tree copied\n");
    } else {
        cfs_mkdir(&g_fs, "BOOT");
        cfs_mkdir(&g_fs, "INSTALL");
        cfs_write(&g_fs, "INSTALL/OK", "chrisos\n", 8u);
    }
    cfs_sync(&g_fs);
    serial_puts("install gpt+esp+cfs disk=");
    serial_puts(label ? label : "disk");
    serial_puts("\n");
    return 0;
}

int install_disk(int index) {
    if (!bd_installable(index)) {
        serial_puts("install refused\n");
        return -1;
    }
    return install_device(bd_get(index), bd_name(index), 1);
}

static uint8_t *g_ram;
static uint32_t g_ram_secs;

static int ram_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t s;
    (void)ctx;
    for (s = 0; s < count; ++s) {
        uint32_t i;
        if (lba + s < g_ram_secs) {
            for (i = 0; i < 512u; ++i)
                d[s * 512u + i] = g_ram[(lba + s) * 512u + i];
        } else {
            for (i = 0; i < 512u; ++i)
                d[s * 512u + i] = 0;
        }
    }
    return BD_OK;
}

static int ram_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t s;
    (void)ctx;
    for (s = 0; s < count; ++s) {
        uint32_t i;
        if (lba + s >= g_ram_secs)
            continue;
        for (i = 0; i < 512u; ++i)
            g_ram[(lba + s) * 512u + i] = p[s * 512u + i];
    }
    return BD_OK;
}

int install_selftest(void) {
    uint64_t bytes = 70000ull * 512ull;
    BlockDevice bd;
    uint8_t sig[512];
    if (require_boot_files() != 0) {
        serial_puts("install selftest skip\n");
        return 0;
    }
    g_ram = (uint8_t *)kmalloc(bytes);
    if (!g_ram) {
        serial_puts("install ram skip\n");
        return -1;
    }
    g_ram_secs = 70000u;
    {
        uint32_t i;
        uint32_t n = g_ram_secs * (512u / 4u);
        uint32_t *w = (uint32_t *)(void *)g_ram;
        for (i = 0; i < n; ++i)
            w[i] = 0;
    }
    bd.ctx = 0;
    bd.sector_size = 512;
    bd.sector_count = (uint32_t)STOR_DISK_SECTORS + 2048u + 65536u + 64u;
    bd.read = ram_read;
    bd.write = ram_write;
    bd.flush = 0;
    bd.writable = 1;
    if (install_device(&bd, "ram", 0) != 0) {
        serial_puts("install fail\n");
        kfree(g_ram);
        g_ram = 0;
        return -1;
    }
    if (bd_read(&bd, 1, 1, sig) != BD_OK || sig[0] != 'E' || sig[1] != 'F' ||
        sig[2] != 'I') {
        kfree(g_ram);
        g_ram = 0;
        return -1;
    }
    if (bd_read(&bd, 2177u, 1, sig) != BD_OK || sig[0] != 'E' || sig[1] != 'F' ||
        sig[2] != 'I') {
        serial_puts("install esp dir fail\n");
        kfree(g_ram);
        g_ram = 0;
        return -1;
    }
    kfree(g_ram);
    g_ram = 0;
    serial_puts("install selftest ok\n");
    return 0;
}

int install_auto(void) {
    uint32_t size = 0;
    uint16_t type = 0;
    int i;
    if (fs_stat("BOOT/INSTALL.AUTO", &size, &type) != 0)
        return 0;
    for (i = 0; i < bd_count(); ++i) {
        if (!bd_installable(i))
            continue;
        serial_puts("install auto ");
        serial_puts(bd_name(i));
        serial_puts("\n");
        return install_disk(i);
    }
    serial_puts("INSTALL FAIL: no target\n");
    return -1;
}
