#include "machine/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int chris_fb_attach(ChrisMachine *m) {
    if (!m) {
        return -1;
    }
    if (m->ram_size > CHRIS_FB_PHYS) {
        return -1;
    }
    m->fb.base = CHRIS_FB_PHYS;
    m->fb.width = CHRIS_FB_WIDTH;
    m->fb.height = CHRIS_FB_HEIGHT;
    m->fb.pitch = CHRIS_FB_WIDTH * 4u;
    m->fb.size = (size_t)m->fb.pitch * (size_t)m->fb.height;
    m->fb.dirty = 0;
    m->fb.pix = (uint8_t *)calloc(1, m->fb.size);
    return m->fb.pix ? 0 : -1;
}

int chris_fb_get(const ChrisMachine *m, uint32_t x, uint32_t y, uint32_t *pixel) {
    const uint8_t *p;
    if (!m || !pixel || !m->fb.pix || x >= m->fb.width || y >= m->fb.height) {
        return -1;
    }
    p = m->fb.pix + (size_t)y * m->fb.pitch + (size_t)x * 4u;
    memcpy(pixel, p, 4);
    return 0;
}

int chris_fb_dirty(const ChrisMachine *m) {
    return m && m->fb.dirty;
}

static uint32_t png_crc(const uint8_t *data, size_t n) {
    uint32_t c = 0xffffffffu;
    size_t i;
    int b;
    for (i = 0; i < n; ++i) {
        c ^= data[i];
        for (b = 0; b < 8; ++b) {
            uint32_t mask = -(c & 1u);
            c = (c >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~c;
}

static void png_chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t head[8];
    uint32_t crc;
    uint8_t *buf;
    head[0] = (uint8_t)(len >> 24);
    head[1] = (uint8_t)(len >> 16);
    head[2] = (uint8_t)(len >> 8);
    head[3] = (uint8_t)len;
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    if (len) {
        fwrite(data, 1, len, f);
    }
    buf = (uint8_t *)malloc((size_t)len + 4u);
    if (!buf) {
        return;
    }
    memcpy(buf, type, 4);
    if (len) {
        memcpy(buf + 4, data, len);
    }
    crc = png_crc(buf, (size_t)len + 4u);
    free(buf);
    head[0] = (uint8_t)(crc >> 24);
    head[1] = (uint8_t)(crc >> 16);
    head[2] = (uint8_t)(crc >> 8);
    head[3] = (uint8_t)crc;
    fwrite(head, 1, 4, f);
}

static uint32_t adler32(const uint8_t *data, size_t n) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    size_t i;
    for (i = 0; i < n; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

static int write_png(const ChrisMachine *m, const char *path) {
    size_t raw_n = (size_t)m->fb.height * ((size_t)m->fb.width * 3u + 1u);
    size_t blocks = (raw_n + 65535u) / 65535u;
    size_t zlen = 2u + blocks * 5u + raw_n + 4u;
    uint8_t *raw;
    uint8_t *z;
    size_t y;
    size_t x;
    size_t o = 0;
    size_t zo;
    uint32_t sum;
    uint8_t ihdr[13];
    FILE *f;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    raw = (uint8_t *)malloc(raw_n);
    z = (uint8_t *)malloc(zlen);
    if (!raw || !z) {
        free(raw);
        free(z);
        return -1;
    }
    o = 0;
    for (y = 0; y < m->fb.height; ++y) {
        raw[o++] = 0;
        for (x = 0; x < m->fb.width; ++x) {
            uint32_t p = 0;
            memcpy(&p, m->fb.pix + y * m->fb.pitch + x * 4u, 4);
            raw[o++] = (uint8_t)((p >> 16) & 255u);
            raw[o++] = (uint8_t)((p >> 8) & 255u);
            raw[o++] = (uint8_t)(p & 255u);
        }
    }
    zo = 0;
    z[zo++] = 0x78;
    z[zo++] = 0x01;
    o = 0;
    while (o < raw_n) {
        size_t n = raw_n - o;
        uint16_t len;
        if (n > 65535u) {
            n = 65535u;
        }
        len = (uint16_t)n;
        z[zo++] = (o + n == raw_n) ? 1u : 0u;
        z[zo++] = (uint8_t)len;
        z[zo++] = (uint8_t)(len >> 8);
        z[zo++] = (uint8_t)(~len);
        z[zo++] = (uint8_t)((~len) >> 8);
        memcpy(z + zo, raw + o, n);
        zo += n;
        o += n;
    }
    sum = adler32(raw, raw_n);
    z[zo++] = (uint8_t)(sum >> 24);
    z[zo++] = (uint8_t)(sum >> 16);
    z[zo++] = (uint8_t)(sum >> 8);
    z[zo++] = (uint8_t)sum;
    memset(ihdr, 0, sizeof ihdr);
    ihdr[0] = (uint8_t)(m->fb.width >> 24);
    ihdr[1] = (uint8_t)(m->fb.width >> 16);
    ihdr[2] = (uint8_t)(m->fb.width >> 8);
    ihdr[3] = (uint8_t)m->fb.width;
    ihdr[4] = (uint8_t)(m->fb.height >> 24);
    ihdr[5] = (uint8_t)(m->fb.height >> 16);
    ihdr[6] = (uint8_t)(m->fb.height >> 8);
    ihdr[7] = (uint8_t)m->fb.height;
    ihdr[8] = 8;
    ihdr[9] = 2;
    f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return -1;
    }
    fwrite(sig, 1, 8, f);
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)zo);
    png_chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

static int write_ppm(const ChrisMachine *m, const char *path) {
    FILE *f = fopen(path, "wb");
    uint32_t y;
    uint32_t x;
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%u %u\n255\n", m->fb.width, m->fb.height);
    for (y = 0; y < m->fb.height; ++y) {
        for (x = 0; x < m->fb.width; ++x) {
            uint32_t p = 0;
            uint8_t rgb[3];
            memcpy(&p, m->fb.pix + y * m->fb.pitch + x * 4u, 4);
            rgb[0] = (uint8_t)((p >> 16) & 255u);
            rgb[1] = (uint8_t)((p >> 8) & 255u);
            rgb[2] = (uint8_t)(p & 255u);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}

int chris_fb_write_image(const ChrisMachine *m, const char *path) {
    size_t n;
    if (!m || !path || !m->fb.pix) {
        return -1;
    }
    n = strlen(path);
    if (n >= 4 && strcmp(path + n - 4, ".png") == 0) {
        return write_png(m, path);
    }
    return write_ppm(m, path);
}
