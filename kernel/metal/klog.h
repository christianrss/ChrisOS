#ifndef CHRISOS_KLOG_H
#define CHRISOS_KLOG_H

#include <stdint.h>

/* Fixed ring. It does not allocate and it does not touch the filesystem. */
void klog_init(void);
void klog_putc(char value);
void klog_puts(const char *text);
uint32_t klog_copy(char *dst, uint32_t cap);

#endif
