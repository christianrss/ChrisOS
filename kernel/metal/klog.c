#include "klog.h"
#include "spin.h"

#define KLOG_CAP 8192u

static char g_log[KLOG_CAP];
static uint32_t g_pos;
static uint32_t g_len;
static Spinlock g_lock;
static int g_ready;

void klog_init(void) {
    if (g_ready) {
        return;
    }
    spin_init(&g_lock);
    g_pos = 0u;
    g_len = 0u;
    g_ready = 1;
}

void klog_putc(char value) {
    if (!g_ready) {
        klog_init();
    }
    spin_lock(&g_lock);
    g_log[g_pos] = value;
    g_pos++;
    if (g_pos == KLOG_CAP) {
        g_pos = 0u;
    }
    if (g_len < KLOG_CAP) {
        g_len++;
    }
    spin_unlock(&g_lock);
}

void klog_puts(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        klog_putc(*text);
        text++;
    }
}

uint32_t klog_copy(char *dst, uint32_t cap) {
    uint32_t n;
    uint32_t start;
    uint32_t i;

    if (!dst || cap == 0u || !g_ready) {
        return 0u;
    }
    spin_lock(&g_lock);
    n = g_len;
    if (n > cap) {
        n = cap;
    }
    start = (g_pos + KLOG_CAP - n) % KLOG_CAP;
    for (i = 0u; i < n; i++) {
        dst[i] = g_log[(start + i) % KLOG_CAP];
    }
    spin_unlock(&g_lock);
    return n;
}
