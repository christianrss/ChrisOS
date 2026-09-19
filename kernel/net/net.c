#include "net.h"

#include "pit.h"
#include "serial.h"
#include "virtio_net.h"
#include "net_xfer.h"

#define NET_IP0 10u
#define NET_IP1 0u
#define NET_IP2 2u
#define NET_IP3 15u

#define NET_GW0 10u
#define NET_GW1 0u
#define NET_GW2 2u
#define NET_GW3 2u

#define ETH_HDR_LEN   14u
#define IP_PROTO_UDP  17u
#define IP_PROTO_TCP  6u

#define TCP_FLAG_FIN  0x01u
#define TCP_FLAG_SYN  0x02u
#define TCP_FLAG_RST  0x04u
#define TCP_FLAG_PSH  0x08u
#define TCP_FLAG_ACK  0x10u

#define NET_TX_BUF    1600u

static uint8_t g_our_ip[4] = { NET_IP0, NET_IP1, NET_IP2, NET_IP3 };
static uint8_t g_gw_mac[6];
static int g_gw_mac_valid;
static int g_net_ready;

struct TcpConn {
    int active;
    uint8_t remote_mac[6];
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
};

static struct TcpConn g_tcp;

static uint8_t g_tx[NET_TX_BUF];

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static int ip_eq4(const uint8_t *a, const uint8_t *b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int ip_is_us(const uint8_t *ip) {
    return ip_eq4(ip, g_our_ip);
}

static uint16_t csum16(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t index = 0;

    while (len > 1u) {
        sum += (uint16_t)((data[index] << 8) | data[index + 1u]);
        index += 2u;
        len -= 2u;
    }
    if (len != 0u) {
        sum += (uint16_t)(data[index] << 8);
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t ip_checksum(uint8_t *ip, uint32_t ihl) {
    ip[10] = 0;
    ip[11] = 0;
    return csum16(ip, ihl);
}

static uint16_t tcp_checksum(const uint8_t *ip, const uint8_t *tcp, uint32_t tcp_len) {
    uint8_t pseudo[12];
    uint32_t total;
    uint32_t index;
    uint32_t sum = 0;

    pseudo[0] = ip[12];
    pseudo[1] = ip[13];
    pseudo[2] = ip[14];
    pseudo[3] = ip[15];
    pseudo[4] = ip[16];
    pseudo[5] = ip[17];
    pseudo[6] = ip[18];
    pseudo[7] = ip[19];
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)(tcp_len & 0xFFu);

    total = 12u + tcp_len;
    for (index = 0; index + 1u < total; index += 2u) {
        const uint8_t *chunk;
        if (index < 12u) {
            chunk = pseudo + index;
        } else {
            chunk = tcp + (index - 12u);
        }
        sum += (uint16_t)((chunk[0] << 8) | chunk[1]);
    }
    if ((total & 1u) != 0u) {
        const uint8_t *last = tcp + (total - 13u);
        sum += (uint16_t)(last[0] << 8);
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void eth_build(uint8_t *frame, const uint8_t *dst, const uint8_t *src,
                      uint16_t ethertype) {
    uint32_t index;
    for (index = 0; index < 6u; ++index) {
        frame[index] = dst[index];
        frame[6u + index] = src[index];
    }
    write_be16(frame + 12, ethertype);
}

static uint32_t ip_build(uint8_t *ip, const uint8_t *src, const uint8_t *dst,
                         uint8_t proto, uint16_t total_len) {
    ip[0] = 0x45u;
    ip[1] = 0;
    write_be16(ip + 2, total_len);
    write_be16(ip + 4, (uint16_t)(pit_ticks() & 0xFFFFu));
    ip[6] = 0x40u;
    ip[7] = 0;
    ip[8] = 64;
    ip[9] = proto;
    ip[12] = src[0];
    ip[13] = src[1];
    ip[14] = src[2];
    ip[15] = src[3];
    ip[16] = dst[0];
    ip[17] = dst[1];
    ip[18] = dst[2];
    ip[19] = dst[3];
    {
        uint16_t csum = ip_checksum(ip, 20u);
        write_be16(ip + 10, csum);
    }
    return 20u;
}

static void net_send_frame(const uint8_t *frame, uint32_t len) {
    (void)virtio_net_tx(frame, len);
}

static void net_send_arp_reply(const uint8_t *req, uint32_t n) {
    const uint8_t *sha;
    const uint8_t *spa;
    const uint8_t *tpa;
    const uint8_t *our_mac;
    uint32_t out_len;

    if (n < 42u) {
        return;
    }
    sha = req + 22;
    spa = req + 28;
    tpa = req + 38;
    if (!ip_is_us(tpa)) {
        return;
    }

    our_mac = virtio_net_mac();
    eth_build(g_tx, sha, our_mac, 0x0806u);
    g_tx[14] = 0;
    g_tx[15] = 1;
    write_be16(g_tx + 16, 0x0800u);
    g_tx[18] = 6;
    g_tx[19] = 4;
    write_be16(g_tx + 20, 2);
    {
        uint32_t index;
        for (index = 0; index < 6u; ++index) {
            g_tx[22 + index] = our_mac[index];
        }
        g_tx[28] = g_our_ip[0];
        g_tx[29] = g_our_ip[1];
        g_tx[30] = g_our_ip[2];
        g_tx[31] = g_our_ip[3];
        for (index = 0; index < 6u; ++index) {
            g_tx[32 + index] = sha[index];
        }
        g_tx[38] = spa[0];
        g_tx[39] = spa[1];
        g_tx[40] = spa[2];
        g_tx[41] = spa[3];
    }
    out_len = 42u;
    serial_puts("arp reply\n");
    net_send_frame(g_tx, out_len);
}

static void net_arp(const uint8_t *frame, uint32_t n) {
    uint16_t oper;

    if (n < 42u) {
        return;
    }
    oper = read_be16(frame + 20);
    if (oper == 1u) {
        net_send_arp_reply(frame, n);
    } else if (oper == 2u) {
        const uint8_t *spa = frame + 28;
        const uint8_t *sha = frame + 22;
        if (ip_eq4(spa, (const uint8_t[]){ NET_GW0, NET_GW1, NET_GW2, NET_GW3 })) {
            uint32_t index;
            for (index = 0; index < 6u; ++index) {
                g_gw_mac[index] = sha[index];
            }
            g_gw_mac_valid = 1;
        }
    }
}

static void net_udp_echo(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    const uint8_t *udp;
    uint32_t ihl;
    uint16_t ip_total;
    uint16_t dport;
    uint16_t udp_len;
    uint32_t payload_off;
    uint32_t payload_len;
    uint32_t out_ip_off;
    uint32_t out_udp_off;
    uint32_t out_len;
    const uint8_t *our_mac;

    if (n < 42u) {
        return;
    }
    ip = frame + 14;
    if ((ip[0] >> 4) != 4u || ip[9] != IP_PROTO_UDP) {
        return;
    }
    ihl = ((uint32_t)ip[0] & 0x0Fu) * 4u;
    if (ihl < 20u || n < 14u + ihl + 8u) {
        return;
    }
    ip_total = read_be16(ip + 2);
    if (!ip_is_us(ip + 16)) {
        return;
    }
    udp = ip + ihl;
    dport = read_be16(udp + 2);
    if (dport != 7u) {
        return;
    }
    udp_len = read_be16(udp + 4);
    if (udp_len < 8u) {
        return;
    }
    payload_off = 14u + ihl + 8u;
    payload_len = (uint32_t)udp_len - 8u;
    if (14u + ip_total > n || payload_off + payload_len > n) {
        return;
    }

    our_mac = virtio_net_mac();
    eth_build(g_tx, frame + 6, our_mac, 0x0800u);
    out_ip_off = 14u;
    out_udp_off = out_ip_off + 20u;
    (void)ip_build(g_tx + out_ip_off, g_our_ip, ip + 12, IP_PROTO_UDP,
                   (uint16_t)(20u + 8u + payload_len));
    write_be16(g_tx + out_udp_off + 0, read_be16(udp + 2));
    write_be16(g_tx + out_udp_off + 2, read_be16(udp + 0));
    write_be16(g_tx + out_udp_off + 4, (uint16_t)(8u + payload_len));
    write_be16(g_tx + out_udp_off + 6, 0);
    {
        uint32_t index;
        for (index = 0; index < payload_len; ++index) {
            g_tx[out_udp_off + 8u + index] = frame[payload_off + index];
        }
    }
    out_len = out_udp_off + 8u + payload_len;
    serial_puts("udp echo\n");
    net_send_frame(g_tx, out_len);
}

void net_tcp_xmit(const uint8_t *dst_mac, const uint8_t *dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, uint8_t flags,
                         const uint8_t *payload, uint32_t payload_len) {
    uint32_t tcp_len;
    uint32_t ip_total;
    uint32_t out_ip_off;
    uint32_t out_tcp_off;
    uint32_t out_len;
    const uint8_t *our_mac;

    tcp_len = 20u + payload_len;
    ip_total = 20u + tcp_len;
    if (14u + ip_total > NET_TX_BUF) {
        return;
    }

    our_mac = virtio_net_mac();
    eth_build(g_tx, dst_mac, our_mac, 0x0800u);
    out_ip_off = 14u;
    out_tcp_off = out_ip_off + 20u;
    (void)ip_build(g_tx + out_ip_off, g_our_ip, dst_ip, IP_PROTO_TCP,
                   (uint16_t)ip_total);

    write_be16(g_tx + out_tcp_off + 0, src_port);
    write_be16(g_tx + out_tcp_off + 2, dst_port);
    write_be32(g_tx + out_tcp_off + 4, seq);
    write_be32(g_tx + out_tcp_off + 8, ack);
    g_tx[out_tcp_off + 12] = 0x50u;
    g_tx[out_tcp_off + 13] = flags;
    write_be16(g_tx + out_tcp_off + 14, 65535u);
    write_be16(g_tx + out_tcp_off + 16, 0);
    write_be16(g_tx + out_tcp_off + 18, 0);
    {
        uint32_t index;
        for (index = 0; index < payload_len; ++index) {
            g_tx[out_tcp_off + 20u + index] = payload[index];
        }
    }
    {
        uint16_t csum = tcp_checksum(g_tx + out_ip_off, g_tx + out_tcp_off, tcp_len);
        write_be16(g_tx + out_tcp_off + 16, csum);
    }
    out_len = 14u + ip_total;
    net_send_frame(g_tx, out_len);
}

static void net_tcp(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    const uint8_t *tcp;
    uint32_t ihl;
    uint32_t tcp_hdr_len;
    uint32_t payload_off;
    uint32_t payload_len;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    const uint8_t *src_mac;
    const uint8_t *src_ip;

    if (n < 54u) {
        return;
    }
    ip = frame + 14;
    if ((ip[0] >> 4) != 4u || ip[9] != IP_PROTO_TCP) {
        return;
    }
    ihl = ((uint32_t)ip[0] & 0x0Fu) * 4u;
    if (ihl < 20u || n < 14u + ihl + 20u) {
        return;
    }
    if (!ip_is_us(ip + 16)) {
        return;
    }

    tcp = ip + ihl;
    src_port = read_be16(tcp + 0);
    dst_port = read_be16(tcp + 2);
    if (dst_port == NET_XFER_PORT) {
        net_xfer_tcp(frame, n);
        return;
    }
    if (dst_port != 7u) {
        return;
    }
    seq = read_be32(tcp + 4);
    ack = read_be32(tcp + 8);
    flags = tcp[13];
    tcp_hdr_len = ((uint32_t)(tcp[12] >> 4)) * 4u;
    if (tcp_hdr_len < 20u) {
        return;
    }
    payload_off = 14u + ihl + tcp_hdr_len;
    payload_len = 0;
    if (n > payload_off) {
        uint16_t ip_total = read_be16(ip + 2);
        if (14u + ip_total > payload_off) {
            payload_len = 14u + ip_total - payload_off;
        }
    }
    (void)ack;

    src_mac = frame + 6;
    src_ip = ip + 12;

    if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) == 0) {
        g_tcp.active = 1;
        {
            uint32_t index;
            for (index = 0; index < 6u; ++index) {
                g_tcp.remote_mac[index] = src_mac[index];
            }
        }
        g_tcp.remote_ip = read_be32(src_ip);
        g_tcp.remote_port = src_port;
        g_tcp.snd_nxt = (uint32_t)(pit_ticks() * 2654435761u);
        g_tcp.rcv_nxt = seq + 1u;
        net_tcp_xmit(src_mac, src_ip, 7, src_port, g_tcp.snd_nxt, g_tcp.rcv_nxt,
                     (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), 0, 0);
        g_tcp.snd_nxt += 1u;
        serial_puts("tcp syn-ack\n");
        return;
    }

    if (!g_tcp.active || src_port != g_tcp.remote_port ||
        read_be32(src_ip) != g_tcp.remote_ip) {
        return;
    }

    if ((flags & TCP_FLAG_ACK) != 0 && payload_len == 0 &&
        (flags & TCP_FLAG_SYN) == 0 && (flags & TCP_FLAG_FIN) == 0) {
        serial_puts("tcp established\n");
        return;
    }

    if ((flags & TCP_FLAG_PSH) != 0 && payload_len > 0) {
        g_tcp.rcv_nxt = seq + payload_len;
        net_tcp_xmit(g_tcp.remote_mac, src_ip, 7, g_tcp.remote_port,
                     g_tcp.snd_nxt, g_tcp.rcv_nxt,
                     (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                     frame + payload_off, payload_len);
        g_tcp.snd_nxt += payload_len;
        serial_puts("tcp echo\n");
    }
}

static void net_ipv4(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    uint8_t proto;

    if (n < 34u) {
        return;
    }
    ip = frame + 14;
    proto = ip[9];
    if (proto == IP_PROTO_UDP) {
        net_udp_echo(frame, n);
    } else if (proto == IP_PROTO_TCP) {
        net_tcp(frame, n);
    }
}

void net_rx_ethernet(const uint8_t *frame, uint32_t len) {
    uint16_t ethertype;

    if (!g_net_ready || frame == 0 || len < 14u) {
        return;
    }
    ethertype = read_be16(frame + 12);
    if (ethertype == 0x0806u) {
        net_arp(frame, len);
    } else if (ethertype == 0x0800u) {
        net_ipv4(frame, len);
    }
}

int net_init(void) {
    g_net_ready = 0;
    g_gw_mac_valid = 0;
    g_tcp.active = 0;
    if (!virtio_net_init()) {
        return 0;
    }
    g_net_ready = 1;
    net_xfer_init();
    serial_puts("net: ip=");
    serial_write_u64(NET_IP0);
    serial_puts(".");
    serial_write_u64(NET_IP1);
    serial_puts(".");
    serial_write_u64(NET_IP2);
    serial_puts(".");
    serial_write_u64(NET_IP3);
    serial_puts("\n");
    return 1;
}

void net_poll(void) {
    if (g_net_ready) {
        virtio_net_poll();
    }
}

void net_status(char *buf, int cap) {
    int index = 0;
    const uint8_t *mac;

    if (cap < 2) {
        return;
    }
    if (!g_net_ready) {
        buf[0] = 'n';
        buf[1] = 'o';
        buf[2] = 0;
        return;
    }
    mac = virtio_net_mac();
    buf[index++] = 'o';
    buf[index++] = 'k';
    buf[index++] = ' ';
    {
        static const char hex[] = "0123456789ABCDEF";
        int byte_index;
        for (byte_index = 0; byte_index < 6 && index < cap - 1; ++byte_index) {
            if (byte_index > 0 && index < cap - 1) {
                buf[index++] = ':';
            }
            buf[index++] = hex[(mac[byte_index] >> 4) & 0x0F];
            if (index < cap - 1) {
                buf[index++] = hex[mac[byte_index] & 0x0F];
            }
        }
    }
    if (index < cap - 12) {
        buf[index++] = ' ';
        buf[index++] = '1';
        buf[index++] = '0';
        buf[index++] = '.';
        buf[index++] = '0';
        buf[index++] = '.';
        buf[index++] = '2';
        buf[index++] = '.';
        buf[index++] = '1';
        buf[index++] = '5';
    }
    buf[index] = 0;
}
