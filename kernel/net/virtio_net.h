#ifndef CHRISOS_VIRTIO_NET_H
#define CHRISOS_VIRTIO_NET_H

#include <stdint.h>

int virtio_net_init(void);
void virtio_net_poll(void);
int virtio_net_tx(const uint8_t *frame, uint32_t len);
const uint8_t *virtio_net_mac(void);
int virtio_net_ready(void);

#endif
