#ifndef CHRIS_SOCK_H
#define CHRIS_SOCK_H

#include <stdint.h>

void sock_init(void);
void sock_tick(void);
int sock_on_tcp(const uint8_t *frame, uint32_t n);
int sock_on_udp(const uint8_t *frame, uint32_t n);

int sock_listen(uint16_t port);
int sock_accept(int fd);
int sock_connect(uint32_t ip, uint16_t port);
int sock_send(int fd, const uint8_t *buf, int n);
int sock_recv(int fd, uint8_t *buf, int n);
int sock_close(int fd);
/* slot >= 0 selects a CLVM app. Native callers use the wrappers above. */
int sock_listen_for(uint16_t port, int slot);
int sock_accept_for(int fd, int slot);
int sock_connect_for(uint32_t ip, uint16_t port, int slot);
int sock_send_for(int fd, const uint8_t *buf, int n, int slot);
int sock_recv_for(int fd, uint8_t *buf, int n, int slot);
int sock_close_for(int fd, int slot);
void sock_close_slot(int slot);
void sock_close_proc(int pid);
void sock_bind_proc(int fd, int pid);
int sock_dns(const char *name, uint32_t *ip_out);
void host_rebuild_start(void);
void host_rebuild_tick(void);

#endif
