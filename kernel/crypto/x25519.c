#include "x25519.h"

typedef long long i64;
typedef unsigned long long u64;

typedef struct {
    i64 v[16];
} Fe;

static void fe0(Fe *o) {
    int i;
    for (i = 0; i < 16; ++i) o->v[i] = 0;
}

static void car(Fe *o) {
    int i;
    i64 c;
    for (i = 0; i < 16; ++i) {
        o->v[i] += (i64)1 << 16;
        c = o->v[i] >> 16;
        o->v[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o->v[i] -= c << 16;
    }
}

static void add(Fe *o, const Fe *a, const Fe *b) {
    int i;
    for (i = 0; i < 16; ++i) o->v[i] = a->v[i] + b->v[i];
}

static void sub(Fe *o, const Fe *a, const Fe *b) {
    int i;
    for (i = 0; i < 16; ++i) o->v[i] = a->v[i] - b->v[i];
}

static void mul(Fe *o, const Fe *a, const Fe *b) {
    i64 t[31];
    int i, j;
    for (i = 0; i < 31; ++i) t[i] = 0;
    for (i = 0; i < 16; ++i)
        for (j = 0; j < 16; ++j)
            t[i + j] += a->v[i] * b->v[j];
    for (i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; ++i) o->v[i] = t[i];
    car(o);
    car(o);
}

static void sq(Fe *o, const Fe *a) { mul(o, a, a); }

static void inv(Fe *o, const Fe *a) {
    Fe t;
    int i;
    t = *a;
    for (i = 253; i >= 0; --i) {
        sq(&t, &t);
        if (i != 2 && i != 4) {
            mul(&t, &t, a);
        }
    }
    *o = t;
}

static void cswap(Fe *a, Fe *b, int bit) {
    int i;
    i64 m = -(i64)bit;
    for (i = 0; i < 16; ++i) {
        i64 d = m & (a->v[i] ^ b->v[i]);
        a->v[i] ^= d;
        b->v[i] ^= d;
    }
}

static void pack(uint8_t out[32], const Fe *a) {
    Fe t;
    Fe m;
    int i;
    int j;
    int b;
    t = *a;
    car(&t);
    car(&t);
    car(&t);
    for (j = 0; j < 2; ++j) {
        m.v[0] = t.v[0] - 0xffed;
        for (i = 1; i < 15; ++i) {
            m.v[i] = t.v[i] - 0xffff - ((m.v[i - 1] >> 16) & 1);
            m.v[i - 1] &= 0xffff;
        }
        m.v[15] = t.v[15] - 0x7fff - ((m.v[14] >> 16) & 1);
        b = (int)((m.v[15] >> 16) & 1);
        m.v[14] &= 0xffff;
        cswap(&t, &m, 1 - b);
    }
    for (i = 0; i < 16; ++i) {
        out[i * 2] = (uint8_t)(t.v[i] & 255);
        out[i * 2 + 1] = (uint8_t)((t.v[i] >> 8) & 255);
    }
}

static void unpack(Fe *o, const uint8_t in[32]) {
    int i;
    fe0(o);
    for (i = 0; i < 16; ++i)
        o->v[i] = (i64)in[i * 2] + ((i64)in[i * 2 + 1] << 8);
    o->v[15] &= 0x7fff;
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t s[32];
    Fe x1, x2, z2, x3, z3, t0, t1;
    int i;
    for (i = 0; i < 32; ++i) s[i] = scalar[i];
    s[0] &= 248;
    s[31] &= 127;
    s[31] |= 64;
    unpack(&x1, point);
    fe0(&x2); x2.v[0] = 1;
    fe0(&z2);
    x3 = x1;
    fe0(&z3); z3.v[0] = 1;
    {
        int swap = 0;
        for (i = 254; i >= 0; --i) {
            int bit = (s[i >> 3] >> (i & 7)) & 1;
            Fe a, b2, c, d, e, da, cb;
            swap ^= bit;
            cswap(&x2, &x3, swap);
            cswap(&z2, &z3, swap);
            swap = bit;
            add(&a, &x2, &z2);
            sub(&b2, &x2, &z2);
            add(&c, &x3, &z3);
            sub(&d, &x3, &z3);
            mul(&da, &d, &a);
            mul(&cb, &c, &b2);
            add(&x3, &da, &cb);
            sq(&x3, &x3);
            sub(&z3, &da, &cb);
            sq(&z3, &z3);
            mul(&z3, &z3, &x1);
            sq(&x2, &a);
            sq(&e, &b2);
            sub(&t1, &x2, &e);
            {
                Fe k;
                Fe t2;
                fe0(&k);
                k.v[0] = 121665;
                mul(&t2, &t1, &k);
                add(&t2, &t2, &x2);
                mul(&z2, &t1, &t2);
            }
            mul(&x2, &x2, &e);
        }
        cswap(&x2, &x3, swap);
        cswap(&z2, &z3, swap);
    }
    inv(&t0, &z2);
    mul(&x2, &x2, &t0);
    pack(out, &x2);
}
