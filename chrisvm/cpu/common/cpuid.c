#include "chris_arch.h"

/* Deterministic virtual CPUID. The host CPUID is never forwarded.
 * Vendor is the 12-byte x86 string "ChrisCPU    ".
 * Feature bits are only those this round actually implements. */

void chris_cpuid(uint32_t leaf, uint32_t sub, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
                 uint32_t *edx) {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;
    (void)sub;
    if (leaf == 0) {
        a = 1;
        b = 0x69726843u; /* "Chri" */
        d = 0x55504373u; /* "sCPU" */
        c = 0x20202020u; /* "    " */
    } else if (leaf == 1) {
        a = 0x00000610u;
        /* FPU=0 (not implemented). TSC, MSR, CMOV, APIC, PSE. No SSE. */
        d = (1u << 4) | (1u << 5) | (1u << 3) | (1u << 9) | (1u << 15);
        c = 0;
    } else if (leaf == 0x80000000u) {
        a = 0x80000001u;
    } else if (leaf == 0x80000001u) {
        /* Long mode is real for this CPU. NX and SYSCALL are stored in EFER
         * but SYSCALL execution is not implemented, so SCE is not advertised. */
        d = 1u << 29;
    }
    *eax = a;
    *ebx = b;
    *ecx = c;
    *edx = d;
}
