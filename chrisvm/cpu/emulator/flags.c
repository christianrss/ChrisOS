#include "chrisvm.h"

static uint64_t size_mask(int os) {
    if (os == 1) {
        return 0xffull;
    }
    if (os == 2) {
        return 0xffffull;
    }
    if (os == 4) {
        return 0xffffffffull;
    }
    return ~0ull;
}

static uint64_t sign_bit(int os) {
    return 1ull << (os * 8 - 1);
}

static int parity_even(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (v & 1u) == 0;
}

static uint64_t write_status(uint64_t flags, uint64_t result, int os, int cf, int of, int af) {
    uint64_t mask = size_mask(os);
    flags &= ~((1ull << 0) | (1ull << 2) | (1ull << 4) | (1ull << 6) | (1ull << 7) | (1ull << 11));
    if (cf) {
        flags |= 1ull << 0;
    }
    if (parity_even((uint8_t)(result & mask))) {
        flags |= 1ull << 2;
    }
    if (af) {
        flags |= 1ull << 4;
    }
    if ((result & mask) == 0) {
        flags |= 1ull << 6;
    }
    if ((result & sign_bit(os)) != 0) {
        flags |= 1ull << 7;
    }
    if (of) {
        flags |= 1ull << 11;
    }
    flags |= 2ull;
    return flags;
}

uint64_t chris_flags_bin(int alu, uint64_t a, uint64_t b, int os, uint64_t flags, uint64_t *result) {
    uint64_t mask = size_mask(os);
    uint64_t sb = sign_bit(os);
    uint64_t aa = a & mask;
    uint64_t bb = b & mask;
    int cf_in = (flags & 1ull) != 0;
    uint64_t r;
    int cf = 0;
    int of = 0;
    int af = 0;
    int logic = 0;

    if (alu == CHRIS_ALU_ADD || alu == CHRIS_ALU_ADC) {
        unsigned extra = (alu == CHRIS_ALU_ADC && cf_in) ? 1u : 0u;
        unsigned __int128 wide = (unsigned __int128)aa + bb + extra;
        r = (uint64_t)wide & mask;
        cf = (wide >> (os * 8)) != 0;
        of = ((aa ^ r) & (bb ^ r) & sb) != 0;
        af = ((aa ^ bb ^ r) & 0x10ull) != 0;
    } else if (alu == CHRIS_ALU_SUB || alu == CHRIS_ALU_CMP || alu == CHRIS_ALU_SBB) {
        unsigned extra = (alu == CHRIS_ALU_SBB && cf_in) ? 1u : 0u;
        r = (aa - bb - extra) & mask;
        if (extra) {
            cf = (aa < bb) || (aa == bb);
        } else {
            cf = aa < bb;
        }
        of = ((aa ^ bb) & (aa ^ r) & sb) != 0;
        af = ((aa ^ bb ^ r) & 0x10ull) != 0;
    } else {
        logic = 1;
        if (alu == CHRIS_ALU_AND || alu == CHRIS_ALU_TEST) {
            r = aa & bb;
        } else if (alu == CHRIS_ALU_OR) {
            r = aa | bb;
        } else {
            r = aa ^ bb;
        }
        cf = 0;
        of = 0;
        af = 0;
    }
    if (result) {
        *result = r & mask;
    }
    (void)logic;
    return write_status(flags, r, os, cf, of, af);
}

int chris_cc_true(int cc, uint64_t f) {
    int cf = (f & (1ull << 0)) != 0;
    int pf = (f & (1ull << 2)) != 0;
    int zf = (f & (1ull << 6)) != 0;
    int sf = (f & (1ull << 7)) != 0;
    int of = (f & (1ull << 11)) != 0;
    switch (cc & 15) {
    case 0:
        return of;
    case 1:
        return !of;
    case 2:
        return cf;
    case 3:
        return !cf;
    case 4:
        return zf;
    case 5:
        return !zf;
    case 6:
        return cf || zf;
    case 7:
        return !cf && !zf;
    case 8:
        return sf;
    case 9:
        return !sf;
    case 10:
        return pf;
    case 11:
        return !pf;
    case 12:
        return sf != of;
    case 13:
        return sf == of;
    case 14:
        return zf || (sf != of);
    default:
        return !zf && (sf == of);
    }
}
