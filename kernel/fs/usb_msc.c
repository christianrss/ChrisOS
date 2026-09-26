#include "usb_msc.h"

#include "usb.h"

/* UHCI host + BOT mass storage. Not a generic USB stack. See usb.h. */

#include "bdev.h"
#include "hwgate.h"
#include "input.h"
#include "pci.h"
#include "port.h"
#include "serial.h"

typedef struct UsbMsc {
    uint16_t io;
    int pages;
    uint8_t addr;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t tog_in;
    uint8_t tog_out;
    uint32_t sectors;
} UsbMsc;

typedef struct UsbHid {
    int live;
    uint8_t addr;
    uint8_t ep;
    uint8_t toggle;
} UsbHid;

static UsbMsc g_usb;
static UsbHid g_hid;
static int g_ready;

static void uhci_ack(void) {
    if (g_usb.io == 0)
        return;
    /* Writing 1s clears USBSTS so IRQ 11 is not left asserted. */
    outw((uint16_t)(g_usb.io + 2u), 0x00FFu);
}

static void uhci_halt(void) {
    int i;
    if (g_usb.io == 0)
        return;
    outw(g_usb.io, (uint16_t)(inw(g_usb.io) & (uint16_t)~1u));
    for (i = 0; i < 200; ++i) {
        if (inw((uint16_t)(g_usb.io + 2u)) & 0x20u) {
            uhci_ack();
            return;
        }
    }
    uhci_ack();
}

static void uhci_reset(void) {
    int i;
    if (g_usb.io == 0)
        return;
    uhci_halt();
    outw(g_usb.io, 0x0002u);
    for (i = 0; i < 200; ++i) {
        if ((inw(g_usb.io) & 0x0002u) == 0) {
            uhci_ack();
            return;
        }
    }
    uhci_ack();
}

static void delay_ticks(int n) {
    int i;
    int spins = n * 50000;
    for (i = 0; i < spins; ++i) {
        __asm__ volatile("pause");
        if ((i & 4095) == 0 && g_usb.io != 0)
            (void)inw((uint16_t)(g_usb.io + 6u));
    }
}

static uint32_t page_phys(void) {
    return hw_dma_lo(g_usb.pages);
}

static void td_write(uint32_t at, uint32_t link, uint32_t token, uint32_t buf) {
    /* Bit 2 is depth-first. Without it the HC finishes the SETUP TD and
     * returns to the queue head, so the status stage never runs. */
    if ((link & 1u) == 0u)
        link |= 4u;
    hw_dma_w32(g_usb.pages, at, link);
    hw_dma_w32(g_usb.pages, at + 4u, 0x18800000u);
    hw_dma_w32(g_usb.pages, at + 8u, token);
    hw_dma_w32(g_usb.pages, at + 12u, buf);
}

static int td_wait(uint32_t at) {
    uint16_t prev = inw((uint16_t)(g_usb.io + 6u));
    int frames = 0;
    int guard = 0;
    for (;;) {
        uint32_t st = hw_dma_r32(g_usb.pages, at + 4u);
        uint16_t fr;
        if ((st & (1u << 23)) == 0) {
            if (st & 0x007E0000u)
                return -1;
            return 0;
        }
        /* FRNUM is a port read, so QEMU/KVM actually runs the UHCI schedule.
           A pause-only loop never exits and every transfer times out. */
        fr = inw((uint16_t)(g_usb.io + 6u));
        if (fr != prev) {
            prev = fr;
            frames++;
        } else if ((guard & 31) == 0) {
            /* A port read exits KVM long enough for the UHCI frame timer.
             * serial_putc(0) did the same and filled the log with NULs. */
            (void)inb(0x80);
        }
        if (frames > 200 || ++guard > 100000)
            return -2;
    }
}

static uint32_t token(uint8_t pid, uint8_t addr, uint8_t ep, int toggle, int len) {
    uint32_t ml = len <= 0 ? 0x7FFu : (uint32_t)(len - 1);
    return (uint32_t)pid | ((uint32_t)addr << 8) | ((uint32_t)ep << 15) |
           ((uint32_t)toggle << 19) | (ml << 21);
}

static int run_qh(uint32_t first_td) {
    uhci_halt();
    hw_dma_w32(g_usb.pages, 4096u, 1u);
    hw_dma_w32(g_usb.pages, 4100u, first_td);
    outw((uint16_t)(g_usb.io + 2u), 0x00FFu);
    outl(g_usb.io + 8, page_phys());
    outw(g_usb.io, 0x00C1);
    {
        int rc = td_wait(first_td - page_phys());
        uhci_ack();
        return rc;
    }
}

static int control(uint8_t addr, uint8_t reqtype, uint8_t req, uint16_t value,
                   uint16_t index, uint8_t *data, int len, int data_in) {
    uint32_t setup = page_phys() + 8192u;
    uint32_t payload = setup + 16u;
    uint32_t td0 = page_phys() + 4128u;
    int i;
    hw_dma_w32(g_usb.pages, 8192u, (uint32_t)reqtype | ((uint32_t)req << 8) |
                                       ((uint32_t)value << 16));
    hw_dma_w32(g_usb.pages, 8196u, (uint32_t)index | ((uint32_t)len << 16));
    if (!data_in && data && len) {
        for (i = 0; i < len; ++i)
            hw_dma_w32(g_usb.pages, 8208u + (uint32_t)i, 0);
        for (i = 0; i < len; i += 4) {
            uint32_t v = 0;
            int b;
            for (b = 0; b < 4 && i + b < len; ++b)
                v |= (uint32_t)data[i + b] << (8 * b);
            hw_dma_w32(g_usb.pages, 8208u + (uint32_t)i, v);
        }
    }
    td_write(4128u, len > 0 ? page_phys() + 4160u : page_phys() + 4192u,
             token(0x2D, addr, 0, 0, 8), setup);
    if (len > 0) {
        td_write(4160u, page_phys() + 4192u,
                 token(data_in ? 0x69 : 0xE1, addr, 0, 1, len), payload);
    }
    td_write(4192u, 1u, token(data_in ? 0xE1 : 0x69, addr, 0, 1, 0), 0);
    {
        int rc = run_qh(td0);
        if (rc != 0)
            return rc;
    }
    if (len > 0) {
        int rc = td_wait(4160u);
        if (rc != 0)
            return rc;
    }
    {
        int rc = td_wait(4192u);
        if (rc != 0)
            return rc;
    }
    if (data_in && data) {
        for (i = 0; i < len; i += 4) {
            uint32_t v = hw_dma_r32(g_usb.pages, 8208u + (uint32_t)i);
            int b;
            for (b = 0; b < 4 && i + b < len; ++b)
                data[i + b] = (uint8_t)(v >> (8 * b));
        }
    }
    return 0;
}

static int bulk(int is_in, uint8_t *data, int len) {
    uint32_t buf = page_phys() + 12288u;
    uint8_t ep = is_in ? g_usb.ep_in : g_usb.ep_out;
    uint8_t *tog = is_in ? &g_usb.tog_in : &g_usb.tog_out;
    int done = 0;
    while (done < len) {
        int n = len - done;
        int i;
        if (n > 64)
            n = 64;
        if (!is_in) {
            for (i = 0; i < n; i += 4) {
                uint32_t v = 0;
                int b;
                for (b = 0; b < 4 && i + b < n; ++b)
                    v |= (uint32_t)data[done + i + b] << (8 * b);
                hw_dma_w32(g_usb.pages, 12288u + (uint32_t)i, v);
            }
        }
        td_write(4224u, 1u, token(is_in ? 0x69 : 0xE1, g_usb.addr, ep, *tog, n), buf);
        {
            int rc = run_qh(page_phys() + 4224u);
            if (rc != 0)
                return rc;
        }
        *tog = (uint8_t)(*tog ^ 1u);
        if (is_in) {
            for (i = 0; i < n; i += 4) {
                uint32_t v = hw_dma_r32(g_usb.pages, 12288u + (uint32_t)i);
                int b;
                for (b = 0; b < 4 && i + b < n; ++b)
                    data[done + i + b] = (uint8_t)(v >> (8 * b));
            }
        }
        done += n;
    }
    return 0;
}

static int scsi(const uint8_t *cb, int cblen, uint8_t *data, int len, int data_in) {
    uint8_t cbw[31];
    uint8_t csw[13];
    int i;
    for (i = 0; i < 31; ++i)
        cbw[i] = 0;
    cbw[0] = 0x55;
    cbw[1] = 0x53;
    cbw[2] = 0x42;
    cbw[3] = 0x43;
    cbw[4] = 1;
    cbw[8] = (uint8_t)len;
    cbw[9] = (uint8_t)(len >> 8);
    cbw[12] = data_in ? 0x80 : 0x00;
    cbw[14] = (uint8_t)cblen;
    for (i = 0; i < cblen && i < 16; ++i)
        cbw[15 + i] = cb[i];
    {
        int rc = bulk(0, cbw, 31);
        if (rc != 0)
            return rc;
    }
    if (len > 0) {
        int rc = bulk(data_in ? 1 : 0, data, len);
        if (rc != 0)
            return rc;
    }
    {
        int rc = bulk(1, csw, 13);
        if (rc != 0)
            return rc;
    }
    return csw[12] == 0 ? 0 : -1;
}

static int usb_rw(void *ctx, uint32_t lba, uint32_t count, void *buf, int write) {
    UsbMsc *u = (UsbMsc *)ctx;
    uint8_t *bytes = (uint8_t *)buf;
    (void)u;
    while (count) {
        uint8_t cb[10];
        uint32_t n = count > 8u ? 8u : count;
        int i;
        for (i = 0; i < 10; ++i)
            cb[i] = 0;
        cb[0] = write ? 0x2A : 0x28;
        cb[2] = (uint8_t)(lba >> 24);
        cb[3] = (uint8_t)(lba >> 16);
        cb[4] = (uint8_t)(lba >> 8);
        cb[5] = (uint8_t)lba;
        cb[7] = (uint8_t)(n >> 8);
        cb[8] = (uint8_t)n;
        {
            int rc = scsi(cb, 10, bytes, (int)(n * 512u), write ? 0 : 1);
            if (rc == -2)
                return BD_ETIMEOUT;
            if (rc != 0)
                return BD_EIO;
        }
        bytes += n * 512u;
        lba += n;
        count -= n;
    }
    return BD_OK;
}

static int usb_read(void *ctx, uint32_t lba, uint32_t count, void *dst) {
    return usb_rw(ctx, lba, count, dst, 0);
}

static int usb_write(void *ctx, uint32_t lba, uint32_t count, const void *src) {
    return usb_rw(ctx, lba, count, (void *)src, 1);
}

static int hid_in(uint8_t *data, int len) {
    uint32_t buf;
    uint16_t prev;
    int frames = 0;
    int guard = 0;
    uint32_t st;

    if (!g_hid.live || g_usb.pages < 0 || g_usb.io == 0 || !data)
        return 0;
    if (len > 8)
        len = 8;
    if (len < 1)
        return 0;
    buf = page_phys() + 13312u;
    td_write(4256u, 1u, token(0x69, g_hid.addr, g_hid.ep, g_hid.toggle, len), buf);
    uhci_halt();
    hw_dma_w32(g_usb.pages, 4096u, 1u);
    hw_dma_w32(g_usb.pages, 4100u, page_phys() + 4256u);
    outw((uint16_t)(g_usb.io + 2u), 0x00FFu);
    outl(g_usb.io + 8, page_phys());
    outw(g_usb.io, 0x00C1);
    prev = inw((uint16_t)(g_usb.io + 6u));
    for (;;) {
        uint16_t fr;
        st = hw_dma_r32(g_usb.pages, 4260u);
        if ((st & (1u << 23)) == 0)
            break;
        fr = inw((uint16_t)(g_usb.io + 6u));
        if (fr != prev) {
            prev = fr;
            frames++;
        } else if ((guard & 31) == 0) {
            (void)inb(0x80);
        }
        /* Full-speed interrupt interval is 10ms. Stopping at 4 frames
         * aborts the IN before the tablet can post a report. */
        if (frames > 16 || ++guard > 20000) {
            uhci_halt();
            return 0;
        }
    }
    uhci_halt();
    if (st & 0x00760000u)
        return 0;
    if (st & 0x00080000u)
        return 0;
    g_hid.toggle = (uint8_t)(g_hid.toggle ^ 1u);
    {
        int n = (int)((st & 0x7FFu) + 1u);
        int i;
        if (n > len)
            n = len;
        for (i = 0; i < n; i += 4) {
            uint32_t v = hw_dma_r32(g_usb.pages, 13312u + (uint32_t)i);
            int b;
            for (b = 0; b < 4 && i + b < n; ++b)
                data[i + b] = (uint8_t)(v >> (8 * b));
        }
        return n;
    }
}

int usb_tablet_ready(void) {
    return g_hid.live;
}

void usb_tablet_poll(void) {
    uint8_t rep[8];
    int n;
    int x;
    int y;
    int i;

    if (!g_hid.live)
        return;
    for (i = 0; i < 8; ++i)
        rep[i] = 0;
    n = hid_in(rep, 8);
    if (n < 5)
        return;
    x = (int)rep[1] | ((int)rep[2] << 8);
    y = (int)rep[3] | ((int)rep[4] << 8);
    {
        static int logged;
        if (logged < 4) {
            serial_puts("usb tablet ");
            serial_write_u64((uint64_t)x);
            serial_puts(",");
            serial_write_u64((uint64_t)y);
            serial_puts(" n=");
            serial_write_u64((uint64_t)n);
            serial_puts(" btn=");
            serial_write_u64(rep[0]);
            serial_puts("\n");
            logged++;
        }
    }
    input_pointer_absolute(x, y, 32767, 32767, rep[0]);
}

static int port_reset(uint16_t io, int port) {
    uint16_t sc;
    uint16_t preg = (uint16_t)(io + 0x10 + port * 2);
    int tries;
    sc = 0;
    for (tries = 0; tries < 30; ++tries) {
        sc = inw(preg);
        if (sc & 1u)
            break;
        delay_ticks(1);
    }
    if ((sc & 1u) == 0)
        return 0;
    outw(preg, (uint16_t)(sc | (1u << 9)));
    delay_ticks(8);
    sc = inw(preg);
    outw(preg, (uint16_t)((sc | 4u) & (uint16_t)~(1u << 9)));
    delay_ticks(4);
    sc = inw(preg);
    if ((sc & 5u) != 5u) {
        serial_puts("usb sc ");
        serial_write_u64(sc);
        serial_puts("\n");
        return 0;
    }
    return 1;
}

int usb_msc_probe(void) {
    int bus;
    int dev;
    int fn;
    uint8_t cfg[128];
    uint8_t cap[8];
    uint8_t cmd[10];
    int i;
    int ncfg;
    BlockDevice bd;
    if (g_ready)
        return 1;
    g_usb.pages = -1;
    g_usb.io = 0;
    for (bus = 0; bus < 8; ++bus) {
        for (dev = 0; dev < 32; ++dev) {
            for (fn = 0; fn < 8; ++fn) {
                uint32_t id = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                uint32_t cls;
                uint32_t bar;
                int port;
                if ((id & 0xFFFFu) == 0xFFFFu)
                    continue;
                cls = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 8);
                if (((cls >> 8) & 0xFFFFFFu) != 0x0C0300u)
                    continue;
                bar = pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x20);
                if ((bar & 1u) == 0)
                    continue;
                pci_write((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4,
                          pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 4) | 5u);
                g_usb.io = (uint16_t)(bar & ~3u);
                g_usb.pages = hw_dma_alloc_low(4);
                if (g_usb.pages < 0) {
                    serial_puts("usb dma miss\n");
                    return 0;
                }
                serial_puts("usb uhci dma=");
                serial_write_hex(((uint64_t)hw_dma_hi(g_usb.pages) << 32) |
                                 hw_dma_lo(g_usb.pages));
                serial_puts("\n");
                uhci_reset();
                for (i = 0; i < 1024; ++i)
                    hw_dma_w32(g_usb.pages, (uint32_t)i * 4u, page_phys() + 4096u + 2u);
                {
                    uint8_t next_addr = 1;
                    int keep = 0;
                    for (port = 0; port < 2; ++port) {
                        int off;
                        uint8_t addr;
                        uint8_t ep_in = 0;
                        uint8_t ep_out = 0;
                        uint8_t ep_intr = 0;
                        int iface_class = -1;
                        int iface_num = 0;
                        uint8_t cfg_val;
                        if (!port_reset(g_usb.io, port))
                            continue;
                        if (control(0, 0x00, 5, next_addr, 0, 0, 0, 0) != 0) {
                            serial_puts("usb addr\n");
                            continue;
                        }
                        addr = next_addr++;
                        if (control(addr, 0x80, 6, 0x0200, 0, cfg, 9, 1) != 0) {
                            serial_puts("usb desc\n");
                            continue;
                        }
                        ncfg = cfg[2] | (cfg[3] << 8);
                        if (ncfg > 128)
                            ncfg = 128;
                        if (ncfg < 9)
                            continue;
                        cfg_val = cfg[5] ? cfg[5] : 1;
                        if (control(addr, 0x80, 6, 0x0200, 0, cfg, ncfg, 1) != 0) {
                            serial_puts("usb cfg\n");
                            continue;
                        }
                        for (off = 0; off + 2 < ncfg;) {
                            int len = cfg[off];
                            int typ = cfg[off + 1];
                            if (len < 2)
                                break;
                            if (typ == 4 && len >= 9 && off + len <= ncfg) {
                                iface_num = cfg[off + 2];
                                iface_class = cfg[off + 5];
                            }
                            if (typ == 5 && len >= 7 && off + len <= ncfg) {
                                uint8_t ep = (uint8_t)(cfg[off + 2] & 0x0Fu);
                                uint8_t attr = cfg[off + 3];
                                int inn = cfg[off + 2] & 0x80;
                                if ((attr & 3) == 2) {
                                    if (inn)
                                        ep_in = ep;
                                    else
                                        ep_out = ep;
                                } else if ((attr & 3) == 3 && inn && ep_intr == 0) {
                                    ep_intr = ep;
                                }
                            }
                            off += len;
                        }
                        if (ep_in && ep_out && !g_ready) {
                            g_usb.addr = addr;
                            g_usb.ep_in = ep_in;
                            g_usb.ep_out = ep_out;
                            if (control(addr, 0x00, 9, cfg_val, 0, 0, 0, 0) != 0) {
                                serial_puts("usb set\n");
                                continue;
                            }
                            g_usb.tog_in = 0;
                            g_usb.tog_out = 0;
                            for (i = 0; i < 10; ++i)
                                cmd[i] = 0;
                            cmd[0] = 0x25;
                            if (scsi(cmd, 10, cap, 8, 1) != 0) {
                                serial_puts("usb scsi\n");
                                continue;
                            }
                            g_usb.sectors =
                                ((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) |
                                ((uint32_t)cap[2] << 8) | cap[3];
                            g_usb.sectors += 1u;
                            if (g_usb.sectors < 2048u)
                                continue;
                            bd.ctx = &g_usb;
                            bd.sector_size = 512;
                            bd.sector_count = g_usb.sectors;
                            bd.read = usb_read;
                            bd.write = usb_write;
                            bd.flush = 0;
                            bd.writable = 1;
                            bd_add_kind("usb", &bd, BD_USB);
                            g_ready = 1;
                            keep = 1;
                            continue;
                        }
                        if (iface_class == 3 && ep_intr && !g_hid.live) {
                            if (control(addr, 0x00, 9, cfg_val, 0, 0, 0, 0) != 0) {
                                serial_puts("usb hid set\n");
                                continue;
                            }
                            (void)control(addr, 0x21, 0x0A, 0, (uint16_t)iface_num,
                                          0, 0, 0);
                            g_hid.addr = addr;
                            g_hid.ep = ep_intr;
                            g_hid.toggle = 0;
                            g_hid.live = 1;
                            keep = 1;
                        }
                    }
                    if (keep) {
                        uhci_halt();
                        if (g_hid.live)
                            serial_puts("usb hid\n");
                        else
                            serial_puts("usb no hid\n");
                        if (g_ready) {
                            serial_puts("usb msc sectors=");
                            serial_write_u64(g_usb.sectors);
                            serial_puts("\n");
                            return 1;
                        }
                        return 0;
                    }
                }
                /* A live schedule with an active TD wedges later disk I/O. */
                uhci_reset();
                hw_dma_free(g_usb.pages);
                g_usb.pages = -1;
                g_usb.io = 0;
            }
        }
    }
    if (g_usb.pages >= 0) {
        uhci_reset();
        hw_dma_free(g_usb.pages);
        g_usb.pages = -1;
        g_usb.io = 0;
    }
    serial_puts("usb miss\n");
    return 0;
}
