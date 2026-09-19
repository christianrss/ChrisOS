#include "virtio_net.h"

#include "bootinfo.h"
#include "net.h"
#include "pci.h"
#include "pmm.h"
#include "port.h"
#include "serial.h"

#define VIRTIO_PCI_HOST_FEATURES  0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN      8
#define VIRTIO_PCI_QUEUE_NUM        12
#define VIRTIO_PCI_QUEUE_SEL        14
#define VIRTIO_PCI_QUEUE_NOTIFY     16
#define VIRTIO_PCI_STATUS           18
#define VIRTIO_PCI_ISR              19
#define VIRTIO_PCI_DEVICE           20

#define VIRTIO_ACK           1u
#define VIRTIO_DRIVER        2u
#define VIRTIO_DRIVER_OK     4u
#define VIRTIO_FEATURES_OK   8u
#define VIRTIO_FAILED        128u

#define VIRTIO_NET_F_MAC     (1u << 5)
#define VIRTIO_NET_F_STATUS  (1u << 16)
#define VIRTIO_NET_S_LINK_UP 1u

#define VRING_DESC_F_NEXT    1u
#define VRING_DESC_F_WRITE   2u

#define VIRTIO_NET_HDR_SIZE  10u
#define NET_QUEUE_SIZE       8u
#define NET_RX_BUF_SIZE      2048u

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[NET_QUEUE_SIZE];
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[NET_QUEUE_SIZE];
};

struct Vq {
    uint64_t page_phys;
    uint32_t page_count;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint16_t queue_max;
    uint16_t last_used;
};

struct VirtNet {
    uint16_t iobase;
    uint8_t mac[6];
    int ready;
    struct Vq rx;
    struct Vq tx;
    uint64_t rx_buf_phys[NET_QUEUE_SIZE];
    uint8_t *rx_buf_virt[NET_QUEUE_SIZE];
    uint64_t tx_buf_phys;
    uint8_t *tx_buf_virt;
};

static struct VirtNet g_vnet;

static void vio_mb(void) {
    __asm__ volatile ("" ::: "memory");
}

static uint8_t *phys_to_virt(uint64_t phys) {
    return (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
}

static uint32_t vring_bytes(uint16_t num) {
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_offset;
    uint32_t used_bytes;

    desc_bytes = (uint32_t)num * (uint32_t)sizeof(struct vring_desc);
    avail_bytes = 4u + (uint32_t)num * 2u;
    used_offset = (desc_bytes + avail_bytes + (PMM_PAGE - 1u)) & ~(PMM_PAGE - 1u);
    used_bytes = 4u + (uint32_t)num * (uint32_t)sizeof(struct vring_used_elem);
    return used_offset + used_bytes;
}

static void vring_init(struct Vq *vq, uint16_t num, uint64_t page_phys) {
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_offset;
    uint8_t *base;

    vq->page_phys = page_phys;
    vq->queue_max = num;
    vq->last_used = 0;
    base = phys_to_virt(page_phys);
    vq->desc = (struct vring_desc *)base;
    desc_bytes = (uint32_t)num * (uint32_t)sizeof(struct vring_desc);
    vq->avail = (struct vring_avail *)(base + desc_bytes);
    avail_bytes = 4u + (uint32_t)num * 2u;
    used_offset = (desc_bytes + avail_bytes + (PMM_PAGE - 1u)) & ~(PMM_PAGE - 1u);
    vq->used = (struct vring_used *)(base + used_offset);
}

static void vq_zero(struct Vq *vq) {
    uint32_t index;
    uint8_t *base;
    uint32_t total;

    base = phys_to_virt(vq->page_phys);
    total = vring_bytes(vq->queue_max);
    for (index = 0; index < total; ++index) {
        base[index] = 0;
    }
}

static uint16_t vq_queue_size(uint16_t iobase, uint16_t queue_index) {
    outw(iobase + VIRTIO_PCI_QUEUE_SEL, queue_index);
    return inw(iobase + VIRTIO_PCI_QUEUE_NUM);
}

static void vq_attach(uint16_t iobase, uint16_t queue_index, struct Vq *vq) {
    outw(iobase + VIRTIO_PCI_QUEUE_SEL, queue_index);
    outl(iobase + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(vq->page_phys >> 12));
}

static void vq_notify(uint16_t iobase, uint16_t queue_index) {
    outw(iobase + VIRTIO_PCI_QUEUE_NOTIFY, queue_index);
}

static int vq_setup_page(struct Vq *vq, uint16_t queue_size) {
    uint64_t phys;
    uint32_t bytes;
    uint32_t pages;

    if (queue_size < NET_QUEUE_SIZE) {
        return 0;
    }
    bytes = vring_bytes(queue_size);
    pages = (bytes + PMM_PAGE - 1u) / PMM_PAGE;
    phys = pmm_alloc_contig(pages);
    if (phys == 0) {
        return 0;
    }
    vring_init(vq, queue_size, phys);
    vq->page_count = pages;
    vq_zero(vq);
    return 1;
}

static void virtio_status_or(uint16_t iobase, uint8_t bits) {
    uint8_t value;

    value = inb(iobase + VIRTIO_PCI_STATUS);
    outb(iobase + VIRTIO_PCI_STATUS, (uint8_t)(value | bits));
}

static int virtio_negotiate(uint16_t iobase) {
    uint32_t host;
    uint32_t guest;

    outb(iobase + VIRTIO_PCI_STATUS, 0);
    virtio_status_or(iobase, (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));

    host = inl(iobase + VIRTIO_PCI_HOST_FEATURES);
    guest = host & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    outl(iobase + VIRTIO_PCI_GUEST_FEATURES, guest);

    virtio_status_or(iobase, (uint8_t)VIRTIO_FEATURES_OK);
    if ((inb(iobase + VIRTIO_PCI_STATUS) & VIRTIO_FEATURES_OK) == 0) {
        outb(iobase + VIRTIO_PCI_STATUS, VIRTIO_FAILED);
        return 0;
    }
    return 1;
}

static void virtio_read_mac(uint16_t iobase) {
    int index;

    for (index = 0; index < 6; ++index) {
        g_vnet.mac[index] = inb(iobase + VIRTIO_PCI_DEVICE + (uint16_t)index);
    }
}

static int virtio_setup_rx(uint16_t iobase) {
    uint16_t queue_size;
    uint16_t index;

    queue_size = vq_queue_size(iobase, 0);
    if (!vq_setup_page(&g_vnet.rx, queue_size)) {
        return 0;
    }

    for (index = 0; index < NET_QUEUE_SIZE; ++index) {
        g_vnet.rx_buf_phys[index] = pmm_alloc();
        if (g_vnet.rx_buf_phys[index] == 0) {
            return 0;
        }
        g_vnet.rx_buf_virt[index] = phys_to_virt(g_vnet.rx_buf_phys[index]);
        g_vnet.rx.desc[index].addr = g_vnet.rx_buf_phys[index];
        g_vnet.rx.desc[index].len = NET_RX_BUF_SIZE;
        g_vnet.rx.desc[index].flags = VRING_DESC_F_WRITE;
        g_vnet.rx.desc[index].next = 0;
        g_vnet.rx.avail->ring[index] = index;
    }
    g_vnet.rx.avail->idx = NET_QUEUE_SIZE;
    vio_mb();
    vq_attach(iobase, 0, &g_vnet.rx);
    vq_notify(iobase, 0);
    return 1;
}

static int virtio_setup_tx(uint16_t iobase) {
    uint16_t queue_size;

    queue_size = vq_queue_size(iobase, 1);
    if (!vq_setup_page(&g_vnet.tx, queue_size)) {
        return 0;
    }

    g_vnet.tx_buf_phys = pmm_alloc();
    if (g_vnet.tx_buf_phys == 0) {
        return 0;
    }
    g_vnet.tx_buf_virt = phys_to_virt(g_vnet.tx_buf_phys);
    vq_attach(iobase, 1, &g_vnet.tx);
    return 1;
}

static void rx_repost(uint16_t desc_id) {
    uint16_t slot;

    if (desc_id >= NET_QUEUE_SIZE) {
        return;
    }
    slot = g_vnet.rx.avail->idx % g_vnet.rx.queue_max;
    g_vnet.rx.avail->ring[slot] = desc_id;
    g_vnet.rx.avail->idx = (uint16_t)(g_vnet.rx.avail->idx + 1u);
    vio_mb();
    vq_notify(g_vnet.iobase, 0);
}

static void virtio_drain_tx(void) {
    uint32_t spin;
    uint16_t used_idx;

    vio_mb();
    used_idx = g_vnet.tx.used->idx;
    for (spin = 0; spin < 200000u && g_vnet.tx.last_used != used_idx; ++spin) {
        vio_mb();
        used_idx = g_vnet.tx.used->idx;
        if (g_vnet.tx.last_used != used_idx) {
            g_vnet.tx.last_used = (uint16_t)(g_vnet.tx.last_used + 1u);
        }
        (void)inb(g_vnet.iobase + VIRTIO_PCI_ISR);
    }
}

static void virtio_process_rx(void) {
    uint16_t used_idx;

    vio_mb();
    used_idx = g_vnet.rx.used->idx;
    while (g_vnet.rx.last_used != used_idx) {
        struct vring_used_elem elem;
        uint16_t desc_id;
        uint32_t total_len;
        uint8_t *buf;

        vio_mb();
        elem = g_vnet.rx.used->ring[g_vnet.rx.last_used % g_vnet.rx.queue_max];
        g_vnet.rx.last_used = (uint16_t)(g_vnet.rx.last_used + 1u);

        desc_id = (uint16_t)elem.id;
        total_len = elem.len;
        if (desc_id >= NET_QUEUE_SIZE || total_len <= VIRTIO_NET_HDR_SIZE) {
            rx_repost(desc_id);
            continue;
        }

        buf = g_vnet.rx_buf_virt[desc_id];
        if (total_len > VIRTIO_NET_HDR_SIZE) {
            net_rx_ethernet(buf + VIRTIO_NET_HDR_SIZE,
                            total_len - VIRTIO_NET_HDR_SIZE);
        }
        rx_repost(desc_id);
        vio_mb();
        used_idx = g_vnet.rx.used->idx;
    }
}

int virtio_net_init(void) {
    uint16_t iobase;

    g_vnet.ready = 0;
    if (!pci_find_virtio_net(&iobase)) {
        serial_puts("virtio-net: pci device not found\n");
        return 0;
    }
    g_vnet.iobase = iobase;

    if (!virtio_negotiate(iobase)) {
        serial_puts("virtio-net: feature negotiation failed\n");
        return 0;
    }

    virtio_read_mac(iobase);
    if ((inl(iobase + VIRTIO_PCI_HOST_FEATURES) & VIRTIO_NET_F_STATUS) != 0u) {
        outw(iobase + VIRTIO_PCI_DEVICE + 6, VIRTIO_NET_S_LINK_UP);
    }
    if (!virtio_setup_rx(iobase) || !virtio_setup_tx(iobase)) {
        serial_puts("virtio-net: queue setup failed\n");
        outb(iobase + VIRTIO_PCI_STATUS, VIRTIO_FAILED);
        return 0;
    }
    virtio_status_or(iobase, (uint8_t)VIRTIO_DRIVER_OK);
    vq_notify(iobase, 0);

    g_vnet.ready = 1;
    serial_puts("virtio-net ready mac=");
    serial_write_hex((uint64_t)g_vnet.mac[0] << 40 |
                     (uint64_t)g_vnet.mac[1] << 32 |
                     (uint64_t)g_vnet.mac[2] << 24 |
                     (uint64_t)g_vnet.mac[3] << 16 |
                     (uint64_t)g_vnet.mac[4] << 8 |
                     (uint64_t)g_vnet.mac[5]);
    serial_puts("\n");
    return 1;
}

int virtio_net_ready(void) {
    return g_vnet.ready;
}

const uint8_t *virtio_net_mac(void) {
    return g_vnet.mac;
}

int virtio_net_tx(const uint8_t *frame, uint32_t len) {
    uint32_t total;
    uint16_t slot;

    if (!g_vnet.ready || frame == 0 || len == 0 || len > 1514u) {
        return 0;
    }
    virtio_drain_tx();
    total = VIRTIO_NET_HDR_SIZE + len;
    if (total > NET_RX_BUF_SIZE) {
        return 0;
    }

    {
        uint32_t index;
        for (index = 0; index < VIRTIO_NET_HDR_SIZE; ++index) {
            g_vnet.tx_buf_virt[index] = 0;
        }
        for (index = 0; index < len; ++index) {
            g_vnet.tx_buf_virt[VIRTIO_NET_HDR_SIZE + index] = frame[index];
        }
    }

    g_vnet.tx.desc[0].addr = g_vnet.tx_buf_phys;
    g_vnet.tx.desc[0].len = total;
    g_vnet.tx.desc[0].flags = 0;
    g_vnet.tx.desc[0].next = 0;

    slot = g_vnet.tx.avail->idx % g_vnet.tx.queue_max;
    g_vnet.tx.avail->ring[slot] = 0;
    g_vnet.tx.avail->idx = (uint16_t)(g_vnet.tx.avail->idx + 1u);
    vio_mb();
    vq_notify(g_vnet.iobase, 1);
    virtio_drain_tx();

    serial_puts("tx bytes=");
    serial_write_u64((uint64_t)len);
    serial_puts("\n");
    return 1;
}

void virtio_net_poll(void) {
    if (!g_vnet.ready) {
        return;
    }
    (void)inb(g_vnet.iobase + VIRTIO_PCI_ISR);
    virtio_process_rx();
    virtio_drain_tx();
}
