#include "part.h"

/* GUID 43524653-3100-4000-8000-000000000001 "CRFS" */
static const uint8_t g_chrisfs_guid[16] = {
    0x53, 0x46, 0x52, 0x43, 0x00, 0x31, 0x00, 0x40,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
/* Linux filesystem data, accepted so older installer images still mount. */
static const uint8_t g_linux_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

static uint32_t gpt_crc(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    uint32_t i;
    int b;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (b = 0; b < 8; ++b)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

static int guid_eq(const uint8_t *a, const uint8_t *b) {
    int i;
    for (i = 0; i < 16; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

int gpt_find_cfs(BlockDevice *disk, uint32_t *lba, uint32_t *count) {
    uint8_t hdr[512];
    uint8_t ent[512];
    uint8_t crcbuf[92];
    uint32_t i;
    uint32_t nent;
    uint32_t esize;
    uint64_t elba;
    if (!disk || !lba || !count)
        return -1;
    if (bd_read(disk, 1, 1, hdr) != BD_OK)
        return -1;
    if (hdr[0] != 'E' || hdr[1] != 'F' || hdr[2] != 'I' || hdr[3] != ' ' ||
        hdr[4] != 'P' || hdr[5] != 'A' || hdr[6] != 'R' || hdr[7] != 'T')
        return -1;
    for (i = 0; i < 92u; ++i)
        crcbuf[i] = hdr[i];
    crcbuf[16] = crcbuf[17] = crcbuf[18] = crcbuf[19] = 0;
    {
        uint32_t stored = (uint32_t)hdr[16] | ((uint32_t)hdr[17] << 8) |
                          ((uint32_t)hdr[18] << 16) | ((uint32_t)hdr[19] << 24);
        if (gpt_crc(crcbuf, 92) != stored)
            return -1;
    }
    elba = rd64(hdr + 72);
    nent = (uint32_t)hdr[80] | ((uint32_t)hdr[81] << 8) |
           ((uint32_t)hdr[82] << 16) | ((uint32_t)hdr[83] << 24);
    esize = (uint32_t)hdr[84] | ((uint32_t)hdr[85] << 8) |
            ((uint32_t)hdr[86] << 16) | ((uint32_t)hdr[87] << 24);
    if (elba == 0 || elba > 0xFFFFFFFFull || esize < 128u || nent < 1u)
        return -1;
    if (nent > 128u)
        nent = 128u;
    for (i = 0; i < nent; ++i) {
        uint32_t off = (i * esize) % 512u;
        uint32_t sec = (uint32_t)elba + (i * esize) / 512u;
        uint64_t start;
        uint64_t end;
        const uint8_t *e;
        if (off == 0) {
            if (bd_read(disk, sec, 1, ent) != BD_OK)
                return -1;
        }
        e = ent + off;
        if (!guid_eq(e, g_chrisfs_guid) && !guid_eq(e, g_linux_guid))
            continue;
        start = rd64(e + 32);
        end = rd64(e + 40);
        if (start == 0 || start > end || end > 0xFFFFFFFFull)
            continue;
        *lba = (uint32_t)start;
        *count = (uint32_t)(end - start + 1ull);
        return 0;
    }
    return -1;
}

static int part_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    PartView *p = (PartView *)ctx;
    return p->parent->read(p->parent->ctx, p->start + lba, count, dst);
}

static int part_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    PartView *p = (PartView *)ctx;
    return p->parent->write(p->parent->ctx, p->start + lba, count, src);
}

int part_open(PartView *out, BlockDevice *parent, uint32_t start,
              uint32_t count) {
    if (!out || !parent || !count || start >= parent->sector_count)
        return -1;
    if (count > parent->sector_count - start)
        count = parent->sector_count - start;
    out->parent = parent;
    out->start = start;
    out->count = count;
    out->dev.ctx = out;
    out->dev.sector_size = 512;
    out->dev.sector_count = count;
    out->dev.read = part_read;
    out->dev.write = part_write;
    out->dev.flush = 0;
    out->dev.writable = parent->writable;
    return 0;
}
