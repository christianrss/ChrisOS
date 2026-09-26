#include "rng.h"
#include "pit.h"

static uint64_t g_rng = 0x123456789abcdefull;

static int rdrand64(uint64_t *out) {
    uint64_t v;
    unsigned char ok;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) : : "cc");
    if (!ok) {
        return 0;
    }
    *out = v;
    return 1;
}

uint32_t rng_u32(void) {
    uint64_t v;
    if (!rdrand64(&v)) {
        g_rng = g_rng * 6364136223846793005ull + (uint64_t)pit_ticks() + 1u;
        v = g_rng;
    }
    return (uint32_t)v;
}
