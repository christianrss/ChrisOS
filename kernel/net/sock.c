#include "sock.h"

#include "net.h"
#include "pit.h"
#include "port.h"
#include "proc.h"

#define SOCK_MAX 16
#define SOCK_RX 2048

enum {
    SK_FREE = 0,
    SK_LISTEN,
    SK_SYN_SENT,
    SK_ESTABLISHED
};

typedef struct Sock {
    int state;
    int parent;
    uint32_t rip;
    uint16_t rport;
    uint16_t lport;
    uint8_t rmac[6];
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t rx[SOCK_RX];
    int rx_len;
    uint8_t last[256];
    int last_len;
    uint32_t last_tick;
    uint8_t have_mac;
    int owner;
    int slot;
} Sock;

static Sock g_sk[SOCK_MAX];
static uint16_t g_eph = 40000u;

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_ip(uint8_t *p, uint32_t ip) {
    p[0] = (uint8_t)(ip >> 24);
    p[1] = (uint8_t)(ip >> 16);
    p[2] = (uint8_t)(ip >> 8);
    p[3] = (uint8_t)ip;
}

static int alloc_sk(int owner, int slot) {
    int i;
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].state == SK_FREE) {
            g_sk[i].state = SK_ESTABLISHED;
            g_sk[i].rx_len = 0;
            g_sk[i].last_len = 0;
            g_sk[i].parent = 0;
            g_sk[i].owner = owner;
            g_sk[i].slot = slot;
            return i;
        }
    }
    return -1;
}

/* slot >= 0: CLVM app. slot < 0: native process that created the socket. */
static int fd_visible(int fd, int slot) {
    if (fd <= 0 || fd >= SOCK_MAX || g_sk[fd].state == SK_FREE) {
        return 0;
    }
    if (slot >= 0) {
        return g_sk[fd].slot == slot;
    }
    return g_sk[fd].slot < 0 && g_sk[fd].owner == proc_current();
}

void sock_init(void) {
    int i;
    for (i = 0; i < SOCK_MAX; ++i) {
        g_sk[i].state = SK_FREE;
    }
}

static int find_listen(uint16_t port) {
    int i;
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].state == SK_LISTEN && g_sk[i].lport == port) {
            return i;
        }
    }
    return -1;
}

static int find_conn(uint32_t ip, uint16_t rport, uint16_t lport) {
    int i;
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].state == SK_FREE || g_sk[i].state == SK_LISTEN) {
            continue;
        }
        if (g_sk[i].rip == ip && g_sk[i].rport == rport && g_sk[i].lport == lport) {
            return i;
        }
    }
    return -1;
}

static void remember(Sock *s, const uint8_t *pay, int n, uint8_t flags) {
    int i;
    if (n > 250) {
        n = 250;
    }
    s->last[0] = flags;
    s->last[1] = (uint8_t)n;
    for (i = 0; i < n; ++i) {
        s->last[2 + i] = pay ? pay[i] : 0;
    }
    s->last_len = n;
    s->last_tick = (uint32_t)pit_ticks();
}

static void xmit(Sock *s, uint8_t flags, const uint8_t *pay, int n) {
    uint8_t ipb[4];
    uint8_t mac[6];
    const uint8_t *use = s->have_mac ? s->rmac : mac;
    if (!s->have_mac && !net_copy_gw_mac(mac)) {
        return;
    }
    put_ip(ipb, s->rip);
    net_tcp_xmit(use, ipb, s->lport, s->rport, s->snd_nxt, s->rcv_nxt,
                 flags, pay, (uint32_t)n);
    s->last_tick = (uint32_t)pit_ticks();
    if ((flags & 0x02u) && s->state == SK_SYN_SENT) {
        s->last[0] = flags;
        s->last_len = 0;
    } else {
        remember(s, pay, n, flags);
    }
    if (pay && n > 0 && (flags & 0x08u)) {
        s->snd_nxt += (uint32_t)n;
    }
    if (flags & 0x02u) {
        s->snd_nxt += 1u;
    }
}

int sock_listen_for(uint16_t port, int slot) {
    int fd = alloc_sk(proc_current(), slot);
    if (fd < 0) {
        return -1;
    }
    g_sk[fd].state = SK_LISTEN;
    g_sk[fd].lport = port;
    g_sk[fd].parent = 0;
    return fd;
}

int sock_listen(uint16_t port) {
    return sock_listen_for(port, -1);
}

int sock_accept_for(int fd, int slot) {
    int i;
    if (!fd_visible(fd, slot) || g_sk[fd].state != SK_LISTEN) {
        return -1;
    }
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].parent == fd && g_sk[i].state == SK_ESTABLISHED &&
            g_sk[i].rx_len >= 0) {
            g_sk[i].parent = 0;
            return i;
        }
    }
    return -1;
}

int sock_accept(int fd) {
    return sock_accept_for(fd, -1);
}

int sock_connect_for(uint32_t ip, uint16_t port, int slot) {
    int fd = alloc_sk(proc_current(), slot);
    uint8_t mac[6];
    int i;
    if (fd < 0) {
        return -1;
    }
    g_sk[fd].state = SK_SYN_SENT;
    g_sk[fd].rip = ip;
    g_sk[fd].rport = port;
    g_sk[fd].lport = g_eph++;
    if (g_eph < 40000u) {
        g_eph = 40000u;
    }
    g_sk[fd].snd_nxt = (uint32_t)pit_ticks() * 2654435761u;
    g_sk[fd].rcv_nxt = 0;
    g_sk[fd].have_mac = 0;
    if (net_copy_gw_mac(mac)) {
        for (i = 0; i < 6; ++i) {
            g_sk[fd].rmac[i] = mac[i];
        }
        g_sk[fd].have_mac = 1;
    }
    xmit(&g_sk[fd], 0x02u, 0, 0);
    return fd;
}

int sock_connect(uint32_t ip, uint16_t port) {
    return sock_connect_for(ip, port, -1);
}

int sock_send_for(int fd, const uint8_t *buf, int n, int slot) {
    if (!fd_visible(fd, slot) || g_sk[fd].state != SK_ESTABLISHED) {
        return -1;
    }
    if (n < 0) {
        return -1;
    }
    if (n > 200) {
        n = 200;
    }
    xmit(&g_sk[fd], (uint8_t)(0x10u | 0x08u), buf, n);
    return n;
}

int sock_send(int fd, const uint8_t *buf, int n) {
    return sock_send_for(fd, buf, n, -1);
}

int sock_recv_for(int fd, uint8_t *buf, int n, int slot) {
    int i;
    int take;
    if (!fd_visible(fd, slot) || !buf || n <= 0) {
        return -1;
    }
    if (g_sk[fd].state != SK_ESTABLISHED) {
        return -1;
    }
    if (g_sk[fd].rx_len <= 0) {
        return 0;
    }
    take = g_sk[fd].rx_len;
    if (take > n) {
        take = n;
    }
    for (i = 0; i < take; ++i) {
        buf[i] = g_sk[fd].rx[i];
    }
    for (i = take; i < g_sk[fd].rx_len; ++i) {
        g_sk[fd].rx[i - take] = g_sk[fd].rx[i];
    }
    g_sk[fd].rx_len -= take;
    return take;
}

int sock_recv(int fd, uint8_t *buf, int n) {
    return sock_recv_for(fd, buf, n, -1);
}

int sock_close_for(int fd, int slot) {
    if (!fd_visible(fd, slot)) {
        return -1;
    }
    if (g_sk[fd].state == SK_ESTABLISHED) {
        xmit(&g_sk[fd], 0x11u, 0, 0);
    }
    g_sk[fd].state = SK_FREE;
    g_sk[fd].slot = -1;
    return 0;
}

int sock_close(int fd) {
    return sock_close_for(fd, -1);
}

static void sock_close_matching(int (*match)(const Sock *s, int key), int key) {
    int i;
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].state != SK_FREE && match(&g_sk[i], key)) {
            if (g_sk[i].state == SK_ESTABLISHED) {
                xmit(&g_sk[i], 0x11u, 0, 0);
            }
            g_sk[i].state = SK_FREE;
            g_sk[i].slot = -1;
        }
    }
}

static int match_slot(const Sock *s, int key) {
    return s->slot == key;
}

static int match_proc(const Sock *s, int key) {
    return s->owner == key;
}

void sock_close_slot(int slot) {
    if (slot < 0) {
        return;
    }
    sock_close_matching(match_slot, slot);
}

void sock_close_proc(int pid) {
    sock_close_matching(match_proc, pid);
}

int sock_on_tcp(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    const uint8_t *tcp;
    uint32_t ihl;
    uint32_t tcp_hdr;
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint8_t flags;
    uint32_t pay_off;
    uint32_t pay_len;
    uint32_t ip_total;
    int fd;
    int listen;
    int i;

    if (n < 54u) {
        return 0;
    }
    ip = frame + 14;
    ihl = ((uint32_t)ip[0] & 0x0Fu) * 4u;
    if (ihl < 20u || n < 14u + ihl + 20u) {
        return 0;
    }
    tcp = ip + ihl;
    sport = be16(tcp + 0);
    dport = be16(tcp + 2);
    seq = be32(tcp + 4);
    flags = tcp[13];
    tcp_hdr = ((uint32_t)(tcp[12] >> 4)) * 4u;
    pay_off = 14u + ihl + tcp_hdr;
    ip_total = be16(ip + 2);
    pay_len = 0;
    if (14u + ip_total > pay_off) {
        pay_len = 14u + ip_total - pay_off;
    }
    listen = find_listen(dport);
    fd = find_conn(be32(ip + 12), sport, dport);
    if (fd < 0 && listen < 0) {
        return 0;
    }
    if (fd < 0 && (flags & 0x02u) && !(flags & 0x10u)) {
        fd = alloc_sk(g_sk[listen].owner, g_sk[listen].slot);
        if (fd < 0) {
            return 1;
        }
        g_sk[fd].state = SK_ESTABLISHED;
        g_sk[fd].parent = listen;
        g_sk[fd].rip = be32(ip + 12);
        g_sk[fd].rport = sport;
        g_sk[fd].lport = dport;
        g_sk[fd].snd_nxt = (uint32_t)pit_ticks() * 2654435761u;
        g_sk[fd].rcv_nxt = seq + 1u;
        g_sk[fd].have_mac = 1;
        for (i = 0; i < 6; ++i) {
            g_sk[fd].rmac[i] = frame[6 + i];
        }
        xmit(&g_sk[fd], (uint8_t)(0x02u | 0x10u), 0, 0);
        return 1;
    }
    if (fd < 0) {
        return 0;
    }
    if (g_sk[fd].state == SK_SYN_SENT && (flags & 0x12u) == 0x12u) {
        g_sk[fd].rcv_nxt = seq + 1u;
        g_sk[fd].state = SK_ESTABLISHED;
        g_sk[fd].have_mac = 1;
        for (i = 0; i < 6; ++i) {
            g_sk[fd].rmac[i] = frame[6 + i];
        }
        xmit(&g_sk[fd], 0x10u, 0, 0);
        return 1;
    }
    if (pay_len > 0 && pay_off + pay_len <= n) {
        uint32_t room;
        uint32_t copy;
        g_sk[fd].rcv_nxt = seq + pay_len;
        room = (uint32_t)(SOCK_RX - g_sk[fd].rx_len);
        copy = pay_len < room ? pay_len : room;
        for (i = 0; i < (int)copy; ++i) {
            g_sk[fd].rx[g_sk[fd].rx_len++] = frame[pay_off + (uint32_t)i];
        }
        xmit(&g_sk[fd], 0x10u, 0, 0);
        if (g_sk[fd].owner > 0)
            proc_unblock(g_sk[fd].owner);
    }
    return 1;
}

void sock_bind_proc(int fd, int pid) {
    if (fd < 1 || fd >= SOCK_MAX)
        return;
    g_sk[fd].owner = pid;
}

int sock_on_udp(const uint8_t *frame, uint32_t n) {
    const uint8_t *ip;
    const uint8_t *udp;
    uint32_t ihl;
    uint16_t dport;
    uint32_t off;
    uint32_t len;
    int i;
    (void)n;
    ip = frame + 14;
    ihl = ((uint32_t)ip[0] & 0x0Fu) * 4u;
    udp = ip + ihl;
    dport = be16(udp + 0);
    if (dport < 40000u) {
        return 0;
    }
    off = 14u + ihl + 8u;
    len = be16(udp + 4);
    if (len < 8u) {
        return 0;
    }
    len -= 8u;
    for (i = 1; i < SOCK_MAX; ++i) {
        if (g_sk[i].state == SK_LISTEN && g_sk[i].lport == dport &&
            g_sk[i].rx_len == 0) {
            int c = (int)len;
            int k;
            if (c > SOCK_RX) {
                c = SOCK_RX;
            }
            for (k = 0; k < c; ++k) {
                g_sk[i].rx[k] = frame[off + (uint32_t)k];
            }
            g_sk[i].rx_len = c;
            if (g_sk[i].owner > 0)
                proc_unblock(g_sk[i].owner);
            return 1;
        }
    }
    return 0;
}

void sock_tick(void) {
    int i;
    uint32_t now = (uint32_t)pit_ticks();
    for (i = 1; i < SOCK_MAX; ++i) {
        Sock *s = &g_sk[i];
        if (s->state != SK_SYN_SENT && s->state != SK_ESTABLISHED) {
            continue;
        }
        if (s->last_len < 0) {
            continue;
        }
        if (s->state == SK_SYN_SENT && now - s->last_tick > 30u) {
            uint8_t ipb[4];
            uint8_t mac[6];
            const uint8_t *use;
            uint32_t seq = s->snd_nxt - 1u;
            if (!s->have_mac && !net_copy_gw_mac(mac)) {
                continue;
            }
            use = s->have_mac ? s->rmac : mac;
            put_ip(ipb, s->rip);
            net_tcp_xmit(use, ipb, s->lport, s->rport, seq, s->rcv_nxt,
                         0x02u, 0, 0);
            s->last_tick = now;
        }
    }
}

static int dns_fd = -1;

int sock_dns(const char *name, uint32_t *ip_out) {
    uint8_t q[128];
    int n = 0;
    int i;
    uint8_t dns_ip[4];
    const char *p;
    if (!name || !ip_out) {
        return -1;
    }
    if (dns_fd >= 0 && g_sk[dns_fd].rx_len > 12) {
        uint8_t *r = g_sk[dns_fd].rx;
        int len = g_sk[dns_fd].rx_len;
        int k;
        for (k = 12; k + 16 < len; ++k) {
            if (r[k] == 0 && r[k + 1] == 1 && r[k + 2] == 0 && r[k + 3] == 1) {
                int rd = k + 10;
                if (rd + 4 <= len) {
                    *ip_out = be32(r + rd);
                    g_sk[dns_fd].rx_len = 0;
                    return 1;
                }
            }
        }
    }
    if (dns_fd < 0) {
        dns_fd = alloc_sk(proc_current(), -1);
        if (dns_fd < 0) {
            return -1;
        }
        g_sk[dns_fd].state = SK_LISTEN;
        g_sk[dns_fd].lport = g_eph++;
    }
    q[n++] = 0x12;
    q[n++] = 0x34;
    q[n++] = 0x01;
    q[n++] = 0x00;
    q[n++] = 0;
    q[n++] = 1;
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 0;
    p = name;
    while (*p && n < 100) {
        const char *dot = p;
        int lab;
        while (*dot && *dot != '.') {
            dot++;
        }
        lab = (int)(dot - p);
        if (lab <= 0 || lab > 63) {
            break;
        }
        q[n++] = (uint8_t)lab;
        for (i = 0; i < lab; ++i) {
            q[n++] = (uint8_t)p[i];
        }
        p = *dot ? dot + 1 : dot;
    }
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 1;
    q[n++] = 0;
    q[n++] = 1;
    dns_ip[0] = 10;
    dns_ip[1] = 0;
    dns_ip[2] = 2;
    dns_ip[3] = 3;
    if (net_udp_send(dns_ip, g_sk[dns_fd].lport, 53, q, (uint32_t)n) < 0) {
        return -1;
    }
    *ip_out = 0;
    return 0;
}

static int g_rb;
static int g_rb_fd = -1;
static int g_rb_sent;

void host_rebuild_start(void) {
    g_rb = 1;
    g_rb_fd = -1;
    g_rb_sent = 0;
}

void host_rebuild_tick(void) {
    uint8_t buf[16];
    int n;
    static const uint8_t msg[8] = {
        'r', 'e', 'b', 'u', 'i', 'l', 'd', '\n'
    };
    if (!g_rb) {
        return;
    }
    if (g_rb_fd < 0) {
        g_rb_fd = sock_connect(0x0A000202u, 9017);
        return;
    }
    if (!g_rb_sent) {
        if (sock_send(g_rb_fd, msg, 8) > 0) {
            g_rb_sent = 1;
        }
        return;
    }
    n = sock_recv(g_rb_fd, buf, (int)sizeof(buf));
    if (n > 0 && buf[0] == 'O') {
        g_rb = 0;
        machine_reboot();
    }
}
