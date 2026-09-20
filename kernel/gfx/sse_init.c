#include "sse_init.h"

#include <stdint.h>

#define CR0_EM (1u << 2)
#define CR0_TS (1u << 3)
#define CR4_OSFXSR (1u << 9)
#define CR4_OSXMMEXCPT (1u << 10)

static int g_fpu_ready;

#ifdef __freestanding__

static uint64_t read_cr0(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(v));
    return v;
}

static void write_cr0(uint64_t v) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(v) : "memory");
}

static uint64_t read_cr4(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(v));
    return v;
}

static void write_cr4(uint64_t v) {
    __asm__ volatile ("mov %0, %%cr4" : : "r"(v) : "memory");
}

void sse_bsp_init(void) {
    uint64_t cr0 = read_cr0();
    uint64_t cr4 = read_cr4();

    cr0 &= ~(uint64_t)(CR0_EM | CR0_TS);
    cr4 |= (uint64_t)(CR4_OSFXSR | CR4_OSXMMEXCPT);
    write_cr0(cr0);
    write_cr4(cr4);
    g_fpu_ready = 1;
}

int sse_bsp_ready(void) {
    uint64_t cr0;
    uint64_t cr4;

    if (!g_fpu_ready)
        return 0;
    cr0 = read_cr0();
    cr4 = read_cr4();
    if ((cr0 & CR0_EM) != 0 || (cr0 & CR0_TS) != 0)
        return 0;
    if ((cr4 & CR4_OSFXSR) == 0)
        return 0;
    return 1;
}

#else

static int host_sse_available(void) {
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    eax = 1;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(eax));
    return (edx & (1u << 25)) != 0;
}

void sse_bsp_init(void) {
    g_fpu_ready = host_sse_available() ? 1 : 0;
}

int sse_bsp_ready(void) {
    return g_fpu_ready;
}

#endif
