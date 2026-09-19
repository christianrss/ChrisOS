/* LEARN:HX23-04..05 */
#include "net_xfer.h"

#include "fs.h"
#include "heap.h"
#include "pit.h"
#include "serial.h"
#include "storage_limits.h"

extern void net_tcp_xmit(const uint8_t *dst_mac, const uint8_t *dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, uint8_t flags,
                         const uint8_t *payload, uint32_t payload_len);

typedef enum {
    XFER_IDLE = 0,
    XFER_HDR,
    XFER_PATH,
    XFER_DATA,
    XFER_DONE
} XferPhase;

typedef struct {
    int active;
    uint8_t remote_mac[6];
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    XferPhase phase;
    uint32_t path_len;
    uint32_t file_size;
    uint32_t path_got;
    uint32_t data_got;
    char path[NET_XFER_PATH_MAX];
    uint8_t *buf;
} XferConn;

static XferConn g_xfer;

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void xfer_reset(void) {
    if (g_xfer.buf) {
        kfree(g_xfer.buf);
    }
    g_xfer.active = 0;
    g_xfer.phase = XFER_IDLE;
    g_xfer.path_len = 0u;
    g_xfer.file_size = 0u;
    g_xfer.path_got = 0u;
    g_xfer.data_got = 0u;
    g_xfer.buf = 0;
}

void net_xfer_init(void) {
    xfer_reset();
}

static void path_upper(char *path, uint32_t n) {
    uint32_t i;
    for (i = 0u; i < n; i++) {
        char c = path[i];
        if (c >= 'a' && c <= 'z') {
            path[i] = (char)(c - 'a' + 'A');
        }
    }
}

static void xfer_send_ack(uint8_t code) {
    uint8_t b = code;
    net_tcp_xmit(g_xfer.remote_mac, (const uint8_t *)&g_xfer.remote_ip,
                 NET_XFER_PORT, g_xfer.remote_port,
                 g_xfer.snd_nxt, g_xfer.rcv_nxt,
                 (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH), &b, 1u);
    g_xfer.snd_nxt += 1u;
}

static int xfer_finish(void) {
    int n;
    path_upper(g_xfer.path, g_xfer.path_len);
    n = fs_write(g_xfer.path, g_xfer.buf, (int)g_xfer.file_size);
    if (n != (int)g_xfer.file_size) {
        serial_puts("xfer: fs_write failed\n");
        xfer_send_ack(NET_XFER_ACK_ERR);
        xfer_reset();
        return 0;
    }
    serial_puts("xfer: ok ");
    serial_puts(g_xfer.path);
    serial_puts("\n");
    xfer_send_ack(NET_XFER_ACK_OK);
    xfer_reset();
    return 1;
}

static void xfer_consume(const uint8_t *data, uint32_t len) {
    uint32_t i = 0u;
    while (i < len) {
        if (g_xfer.phase == XFER_HDR) {
            static uint8_t hdr[NET_XFER_HDR_SIZE];
            uint32_t need = NET_XFER_HDR_SIZE - g_xfer.path_got;
            uint32_t n = len - i;
            if (n > need) {
                n = need;
            }
            {
                uint32_t j;
                for (j = 0u; j < n; j++) {
                    hdr[g_xfer.path_got + j] = data[i + j];
                }
            }
            g_xfer.path_got += n;
            i += n;
            if (g_xfer.path_got < NET_XFER_HDR_SIZE) {
                continue;
            }
            if (hdr[0] != NET_XFER_MAGIC0 || hdr[1] != NET_XFER_MAGIC1 ||
                hdr[2] != NET_XFER_MAGIC2 || hdr[3] != NET_XFER_MAGIC3) {
                xfer_send_ack(NET_XFER_ACK_ERR);
                xfer_reset();
                return;
            }
            g_xfer.path_len = read_be32(hdr + 4u);
            g_xfer.file_size = read_be32(hdr + 8u);
            g_xfer.path_got = 0u;
            if (g_xfer.path_len == 0u || g_xfer.path_len >= NET_XFER_PATH_MAX ||
                g_xfer.file_size == 0u ||
                g_xfer.file_size > CFS_MAX_FILE_SIZE) {
                xfer_send_ack(NET_XFER_ACK_ERR);
                xfer_reset();
                return;
            }
            g_xfer.buf = (uint8_t *)kmalloc(g_xfer.file_size);
            if (!g_xfer.buf) {
                serial_puts("xfer: nomem\n");
                xfer_send_ack(NET_XFER_ACK_ERR);
                xfer_reset();
                return;
            }
            g_xfer.phase = XFER_PATH;
            continue;
        }
        if (g_xfer.phase == XFER_PATH) {
            uint32_t need = g_xfer.path_len - g_xfer.path_got;
            uint32_t n = len - i;
            if (n > need) {
                n = need;
            }
            {
                uint32_t j;
                for (j = 0u; j < n; j++) {
                    g_xfer.path[g_xfer.path_got + j] = (char)data[i + j];
                }
            }
            g_xfer.path_got += n;
            i += n;
            g_xfer.path[g_xfer.path_len] = '\0';
            if (g_xfer.path_got < g_xfer.path_len) {
                continue;
            }
            g_xfer.phase = XFER_DATA;
            g_xfer.data_got = 0u;
            continue;
        }
        if (g_xfer.phase == XFER_DATA) {
            uint32_t need = g_xfer.file_size - g_xfer.data_got;
            uint32_t n = len - i;
            if (n > need) {
                n = need;
            }
            {
                uint32_t j;
                for (j = 0u; j < n; j++) {
                    g_xfer.buf[g_xfer.data_got + j] = data[i + j];
                }
            }
            g_xfer.data_got += n;
            i += n;
            if (g_xfer.data_got < g_xfer.file_size) {
                continue;
            }
            g_xfer.phase = XFER_DONE;
            xfer_finish();
            return;
        }
        return;
    }
}

void net_xfer_tcp(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    const uint8_t *tcp;
    uint32_t ihl;
    uint32_t tcp_hdr_len;
    uint32_t payload_off;
    uint32_t payload_len;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t seq;
    uint8_t flags;
    const uint8_t *src_mac;
    const uint8_t *src_ip;

    if (n < 54u) {
        return;
    }
    ip = frame + 14;
    if ((ip[0] >> 4) != 4u || ip[9] != 6u) {
        return;
    }
    ihl = ((uint32_t)ip[0] & 0x0Fu) * 4u;
    if (ihl < 20u || n < 14u + ihl + 20u) {
        return;
    }

    tcp = ip + ihl;
    src_port = read_be16(tcp + 0);
    dst_port = read_be16(tcp + 2);
    if (dst_port != NET_XFER_PORT) {
        return;
    }
    seq = read_be32(tcp + 4);
    flags = tcp[13];
    tcp_hdr_len = ((uint32_t)(tcp[12] >> 4)) * 4u;
    if (tcp_hdr_len < 20u) {
        return;
    }
    payload_off = 14u + ihl + tcp_hdr_len;
    payload_len = 0u;
    if (n > payload_off) {
        uint16_t ip_total = read_be16(ip + 2);
        if (14u + ip_total > payload_off) {
            payload_len = 14u + ip_total - payload_off;
        }
    }

    src_mac = frame + 6;
    src_ip = ip + 12;

    if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) == 0) {
        xfer_reset();
        g_xfer.active = 1;
        {
            uint32_t index;
            for (index = 0u; index < 6u; index++) {
                g_xfer.remote_mac[index] = src_mac[index];
            }
        }
        g_xfer.remote_ip = read_be32(src_ip);
        g_xfer.remote_port = src_port;
        g_xfer.snd_nxt = (uint32_t)(pit_ticks() * 2654435761u);
        g_xfer.rcv_nxt = seq + 1u;
        g_xfer.phase = XFER_HDR;
        g_xfer.path_got = 0u;
        net_tcp_xmit(src_mac, src_ip, NET_XFER_PORT, src_port,
                     g_xfer.snd_nxt, g_xfer.rcv_nxt,
                     (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), 0, 0);
        g_xfer.snd_nxt += 1u;
        serial_puts("xfer: syn-ack\n");
        return;
    }

    if (!g_xfer.active || src_port != g_xfer.remote_port ||
        read_be32(src_ip) != g_xfer.remote_ip) {
        return;
    }

    if ((flags & TCP_FLAG_ACK) != 0 && payload_len == 0 &&
        (flags & TCP_FLAG_SYN) == 0 && (flags & TCP_FLAG_FIN) == 0) {
        return;
    }

    if (payload_len > 0u) {
        g_xfer.rcv_nxt = seq + payload_len;
        xfer_consume(frame + payload_off, payload_len);
        net_tcp_xmit(g_xfer.remote_mac, src_ip, NET_XFER_PORT, g_xfer.remote_port,
                     g_xfer.snd_nxt, g_xfer.rcv_nxt, TCP_FLAG_ACK, 0, 0);
    }
}
