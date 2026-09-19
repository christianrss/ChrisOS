#ifndef CHRISOS_NET_H
#define CHRISOS_NET_H

#include <stdint.h>

int net_init(void);
void net_poll(void);
void net_status(char *buf, int cap);
void net_rx_ethernet(const uint8_t *frame, uint32_t len);

#endif
