#ifndef CHRISOS_NET_H
#define CHRISOS_NET_H

#include <stdint.h>

int net_init(void);
void net_poll(void);
void net_status(char *buf, int cap);
void net_rx_ethernet(const uint8_t *frame, uint32_t len);
void net_tcp_xmit(const uint8_t *dst_mac, const uint8_t *dst_ip,
                  uint16_t src_port, uint16_t dst_port,
                  uint32_t seq, uint32_t ack, uint8_t flags,
                  const uint8_t *payload, uint32_t payload_len);
int net_udp_send(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const uint8_t *payload, uint32_t payload_len);
int net_copy_gw_mac(uint8_t mac[6]);
const uint8_t *net_our_ip(void);

#endif
