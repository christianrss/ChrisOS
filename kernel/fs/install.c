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

static int put_sec(BlockDevice *d, uint32_t lba, const uint8_t *src) {
    return bd_write(d, lba, 1, src);
}

static const char g_conf[] =
    "timeout: 3\n"
    "/ChrisOS\n"
    "    protocol: limine\n"
    "    path: boot():/KERNEL.ELF\n";

static void fat_name(uint8_t ent[32], const char *shortname, uint16_t cluster,
                     uint32_t size) {
    int i;
    zero(ent, 32);
    for (i = 0; i < 11; ++i)
        ent[i] = (uint8_t)shortname[i];
    ent[11] = 0x20;
    ent[26] = (uint8_t)cluster;
    ent[27] = (uint8_t)(cluster >> 8);
    wr32(ent + 28, size);
}

static int write_esp(BlockDevice *disk, uint32_t esp_lba, uint32_t esp_secs,
                     uint32_t spf) {
    uint32_t i;
    uint32_t root = 1u + spf * 2u;
    uint32_t data = root + 32u;
    uint32_t cluster = 2;
    uint8_t ent[32];
    uint32_t conf_len = 0;
    const char *paths[2];
    const char *names[2];
    int file;
    while (g_conf[conf_len])
        conf_len += 1;
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
    zero(g_chunk, 2048);
    for (i = 0; i < conf_len && i < 2048u; ++i)
        g_chunk[i] = (uint8_t)g_conf[i];
    for (i = 0; i < 4u; ++i) {
        if (put_sec(disk, esp_lba + data + i, g_chunk + i * 512u) != BD_OK)
            return -1;
    }
    paths[0] = "BOOT/KERNEL.ELF";
    names[0] = "KERNEL  ELF";
    paths[1] = "BOOT/BOOTX64.EFI";
    names[1] = "BOOTX64 EFI";
    cluster = 3;
    zero(g_chunk, 2048);
    fat_name(ent, "LIMINE  CFG", 2, conf_len);
    zero(g_sec, 512);
    for (i = 0; i < 32; ++i)
        g_sec[i] = ent[i];
    for (file = 0; file < 2; ++file) {
        uint32_t size = 0;
        uint16_t type = 0;
        uint32_t off = 0;
        uint32_t first = cluster;
        uint32_t slots;
        if (fs_stat(paths[file], &size, &type) != 0 || size == 0 || size > 8u * 1024u * 1024u)
            continue;
        slots = (size + 2047u) / 2048u;
        if (cluster + slots > 4000u)
            continue;
        while (off < size) {
            int n = fs_read_at(paths[file], off, g_chunk, 2048);
            uint32_t s;
            if (n <= 0)
                break;
            for (s = 0; s < 4; ++s) {
                if (put_sec(disk, esp_lba + data + (cluster - 2u) * 4u + s,
                            g_chunk + s * 512u) != BD_OK)
                    return -1;
            }
            g_fat[cluster] = (off + 2048u >= size) ? 0xFFFFu : (uint16_t)(cluster + 1u);
            cluster += 1u;
            off += (uint32_t)n;
        }
        fat_name(ent, names[file], (uint16_t)first, size);
        for (i = 0; i < 32; ++i)
            g_sec[32 + file * 32 + i] = ent[i];
    }
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

int install_disk(int index) {
    BlockDevice *disk;
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
    uint8_t linux_guid[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                              0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
    if (index < 0 || index == bd_boot_index()) {
        serial_puts("install refused boot disk\n");
        return -1;
    }
    disk = bd_get(index);
    if (!disk || !disk->writable)
        return -1;
    sectors = disk->sector_count;
    if (sectors < 40000u)
        return -1;
    if (sectors > 200000u) {
        esp = 65536u;
        spf = 64u;
    }
    cfs_lba = 2048u + esp;
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
        g_entries[128 + i] = linux_guid[i];
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
    if (write_esp(disk, 2048u, esp, spf) != 0)
        return -1;
    if (part_open(&part, disk, cfs_lba, sectors - cfs_lba - 34u) != 0)
        return -1;
    if (cfs_format(&part.dev) != 0)
        return -1;
    if (cfs_mount(&g_fs, &part.dev) != 0)
        return -1;
    cfs_mkdir(&g_fs, "BOOT");
    cfs_write(&g_fs, "BOOT/LIMINE.CFG", g_conf, (uint32_t)sizeof(g_conf) - 1u);
    cfs_mkdir(&g_fs, "INSTALL");
    cfs_write(&g_fs, "INSTALL/OK", "chrisos\n", 8u);
    cfs_sync(&g_fs);
    serial_puts("install gpt+esp+cfs disk=");
    serial_puts(bd_name(index));
    serial_puts("\n");
    return 0;
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
    int idx;
    BlockDevice bd;
    uint8_t sig[512];
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
    idx = bd_add("ram", &bd);
    if (idx < 0)
        return -1;
    if (install_disk(idx) != 0) {
        serial_puts("install fail\n");
        return -1;
    }
    if (bd_read(bd_get(idx), 1, 1, sig) != BD_OK)
        return -1;
    if (sig[0] != 'E' || sig[1] != 'F' || sig[2] != 'I')
        return -1;
    serial_puts("install selftest ok\n");
    return 0;
}
