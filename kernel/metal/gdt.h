#ifndef CHRISOS_GDT_H
#define CHRISOS_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08u
#define GDT_KERNEL_DATA 0x10u
#define GDT_USER_DATA   0x18u
#define GDT_USER_CODE   0x20u
#define GDT_TSS         0x28u

void gdt_init(void);
void gdt_reload_kernel_segments(void);
uint16_t gdt_read_tr(void);

#endif
