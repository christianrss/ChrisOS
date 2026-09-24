#include "sock.h"

#include <stdio.h>

static int g_proc;

int proc_current(void) {
    return g_proc;
}

void proc_unblock(int pid) {
    (void)pid;
}

uint64_t pit_ticks(void) {
    return 1;
}

int net_copy_gw_mac(uint8_t mac[6]) {
    (void)mac;
    return 0;
}

void net_tcp_xmit(const uint8_t *dst_mac, const uint8_t *dst_ip,
                  uint16_t src_port, uint16_t dst_port,
                  uint32_t seq, uint32_t ack, uint8_t flags,
                  const uint8_t *payload, uint32_t payload_len) {
    (void)dst_mac;
    (void)dst_ip;
    (void)src_port;
    (void)dst_port;
    (void)seq;
    (void)ack;
    (void)flags;
    (void)payload;
    (void)payload_len;
}

int net_udp_send(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const uint8_t *payload, uint32_t payload_len) {
    (void)dst_ip;
    (void)src_port;
    (void)dst_port;
    (void)payload;
    (void)payload_len;
    return 0;
}

void machine_reboot(void) {}

static int fail(const char *msg) {
    fprintf(stderr, "fail: %s\n", msg);
    return 1;
}

int main(void) {
    int a;
    int b;
    int native;
    sock_init();
    g_proc = 1;
    a = sock_listen_for(1000, 1);
    b = sock_listen_for(1001, 2);
    if (a < 0 || b < 0) {
        return fail("listen");
    }
    if (sock_close_for(a, 2) == 0) {
        return fail("slot 2 closed slot 1");
    }
    if (sock_accept_for(a, 2) != -1) {
        return fail("slot 2 accepted slot 1");
    }
    if (sock_close_for(a, 1) != 0) {
        return fail("owner close");
    }
    if (sock_close_for(b, 1) == 0) {
        return fail("slot 1 closed slot 2 after");
    }
    sock_close_slot(2);
    if (sock_close_for(b, 2) == 0) {
        return fail("slot close left the socket");
    }

    g_proc = 3;
    native = sock_listen(2000);
    if (native < 0) {
        return fail("native listen");
    }
    g_proc = 4;
    if (sock_close(native) == 0) {
        return fail("other proc closed native socket");
    }
    g_proc = 3;
    if (sock_close(native) != 0) {
        return fail("owner proc close");
    }
    puts("test_sock_owner: ok");
    return 0;
}
