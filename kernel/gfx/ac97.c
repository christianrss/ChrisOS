#include "ac97.h"

#include "bootinfo.h"
#include "irq.h"
#include "pci.h"
#include "pmm.h"
#include "port.h"
#include "proc.h"
#include "serial.h"

#define PCM_RING 2048

static uint16_t g_nam;
static uint16_t g_nabm;
static uint8_t g_irq;
static int g_ready;
static volatile int g_event;
static uint64_t g_buf_phys;
static int16_t *g_buf;
static uint64_t g_bdl_phys;
static uint32_t *g_bdl;
static int16_t g_ring[PCM_RING];
static int g_r;
static int g_w;
static int g_n;

static void ac97_fill(void) {
    int i;
    if (!g_buf) {
        return;
    }
    for (i = 0; i < 1024; ++i) {
        int16_t s = 0;
        if (g_n > 0) {
            s = g_ring[g_r];
            g_r = (g_r + 1) % PCM_RING;
            g_n--;
        }
        g_buf[i] = s;
    }
}

static void ac97_on_irq(struct irq_frame *frame) {
    uint16_t st;
    (void)frame;
    st = inw((uint16_t)(g_nabm + 0x16u));
    outw((uint16_t)(g_nabm + 0x16u), st);
    g_event = 1;
    proc_unblock_why(PROC_ST_BLOCK_IRQ);
    if (g_n <= 0) {
        outb((uint8_t)(g_nabm + 0x1Bu), 0);
        return;
    }
    ac97_fill();
    outb((uint8_t)(g_nabm + 0x15u), 0);
    outb((uint8_t)(g_nabm + 0x1Bu), 0x11u);
}

int ac97_take_event(int irq) {
    if (!g_ready || (uint8_t)irq != g_irq || !g_event) {
        return 0;
    }
    g_event = 0;
    return 1;
}

int ac97_write(const int16_t *samples, int n) {
    int i;
    int put = 0;
    if (!g_ready || !samples || n <= 0) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        if (g_n >= PCM_RING) {
            break;
        }
        g_ring[g_w] = samples[i];
        g_w = (g_w + 1) % PCM_RING;
        g_n++;
        put++;
    }
    if (put > 0) {
        ac97_fill();
        outb((uint8_t)(g_nabm + 0x15u), 0);
        outb((uint8_t)(g_nabm + 0x1Bu), 0x11u);
    }
    return put;
}

int ac97_init(void) {
    uint16_t nam = 0;
    uint16_t nabm = 0;
    uint8_t irq = 0;
    if (!pci_find_ac97(&nam, &nabm, &irq)) {
        serial_puts("ac97: no device\n");
        return 0;
    }
    g_buf_phys = pmm_alloc();
    g_bdl_phys = pmm_alloc();
    if (g_buf_phys == 0 || g_bdl_phys == 0) {
        return 0;
    }
    g_buf = (int16_t *)(uintptr_t)bootinfo_phys_to_virt(g_buf_phys);
    g_bdl = (uint32_t *)(uintptr_t)bootinfo_phys_to_virt(g_bdl_phys);
    g_nam = nam;
    g_nabm = nabm;
    g_irq = irq;
    outw(nam, 0);
    outw((uint16_t)(nam + 0x02u), 0);
    outw((uint16_t)(nam + 0x18u), 0x0808u);
    ac97_fill();
    g_bdl[0] = (uint32_t)g_buf_phys;
    g_bdl[1] = 1024u | (0x8000u << 16);
    outb((uint8_t)(nabm + 0x1Bu), 0x02u);
    outl((uint16_t)(nabm + 0x10u), (uint32_t)g_bdl_phys);
    outb((uint8_t)(nabm + 0x15u), 0);
    if (irq < 16) {
        irq_set_handler(irq, ac97_on_irq);
        pic_set_mask(irq, false);
    }
    /* Do not start the engine here. An empty buffer completes at once and
       the IRQ never lets the desktop loop run. Playback starts on write. */
    g_ready = 1;
    serial_puts("ac97: ready\n");
    return 1;
}
