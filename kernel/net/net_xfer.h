/* LEARN:HX23-03 */
#ifndef CHRIS_NET_XFER_H
#define CHRIS_NET_XFER_H

#include <stdint.h>

#define NET_XFER_PORT      9016u
#define NET_XFER_MAGIC0    'C'
#define NET_XFER_MAGIC1    'F'
#define NET_XFER_MAGIC2    'S'
#define NET_XFER_MAGIC3    '1'
#define NET_XFER_HDR_SIZE  12u
#define NET_XFER_PATH_MAX  512u
#define NET_XFER_ACK_OK    0x06u
#define NET_XFER_ACK_ERR   0x15u

#define TCP_FLAG_FIN  0x01u
#define TCP_FLAG_SYN  0x02u
#define TCP_FLAG_RST  0x04u
#define TCP_FLAG_PSH  0x08u
#define TCP_FLAG_ACK  0x10u

void net_xfer_init(void);
void net_xfer_tcp(const uint8_t *frame, uint32_t n);

#endif
