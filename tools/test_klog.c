#include <stdio.h>
#include <string.h>

#include "klog.h"

int main(void) {
    char buf[64];
    char big[9000];
    uint32_t n;
    uint32_t i;

    klog_init();
    klog_puts("alpha\n");
    klog_putc('b');
    n = klog_copy(buf, sizeof buf);
    if (n != 7 || memcmp(buf, "alpha\nb", 7) != 0) {
        fprintf(stderr, "klog basic copy\n");
        return 1;
    }
    for (i = 0; i < 8300u; i++) {
        klog_putc((char)('A' + (i % 26)));
    }
    n = klog_copy(big, sizeof big);
    if (n != 8192u) {
        fprintf(stderr, "klog cap %u\n", n);
        return 1;
    }
    if (big[n - 1] != (char)('A' + ((8300u - 1u) % 26u))) {
        fprintf(stderr, "klog tail\n");
        return 1;
    }
    printf("klog tests passed\n");
    return 0;
}
