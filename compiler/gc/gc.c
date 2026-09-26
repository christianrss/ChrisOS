#include "gc.h"

#ifdef __freestanding__
#include "heap.h"
#define GCMALLOC kmalloc
#define GCFREE kfree
#else
#include <stdlib.h>
#define GCMALLOC malloc
#define GCFREE free
#endif

#define GC_MAX 4096
#define GC_NURSERY 65536u

typedef struct GcObj {
    uint32_t type_id;
    uint32_t size;
    uint8_t marked;
    uint8_t nurs;
    uint8_t *payload;
} GcObj;

typedef struct GcType {
    int used;
    uint32_t type_id;
    uint32_t size;
    uint32_t gc_bits;
    char name[32];
} GcType;

static GcObj g_obj[GC_MAX];
static int g_n;
static void **g_roots[256];
static int g_nroots;
static uint8_t g_nursery[GC_NURSERY];
static uint32_t g_nused;
static int g_req;
static GcType g_types[GC_TYPE_MAX];
static int g_ntypes;

void gc_collect(void);

void gc_type_clear(void) {
    int i;
    for (i = 0; i < GC_TYPE_MAX; ++i)
        g_types[i].used = 0;
    g_ntypes = 0;
}

int gc_type_register(uint32_t type_id, uint32_t size, uint32_t gc_bits,
                     const char *name) {
    int i;
    int slot = -1;
    for (i = 0; i < GC_TYPE_MAX; ++i) {
        if (g_types[i].used && g_types[i].type_id == type_id) {
            g_types[i].size = size;
            g_types[i].gc_bits = gc_bits;
            if (name) {
                int k = 0;
                while (name[k] && k < 31) {
                    g_types[i].name[k] = name[k];
                    k++;
                }
                g_types[i].name[k] = 0;
            }
            return 1;
        }
        if (!g_types[i].used && slot < 0)
            slot = i;
    }
    if (slot < 0)
        return 0;
    g_types[slot].used = 1;
    g_types[slot].type_id = type_id;
    g_types[slot].size = size;
    g_types[slot].gc_bits = gc_bits;
    g_types[slot].name[0] = 0;
    if (name) {
        int k = 0;
        while (name[k] && k < 31) {
            g_types[slot].name[k] = name[k];
            k++;
        }
        g_types[slot].name[k] = 0;
    }
    g_ntypes++;
    return 1;
}

int gc_type_lookup(uint32_t type_id, uint32_t *size_out, uint32_t *gc_bits_out) {
    int i;
    for (i = 0; i < GC_TYPE_MAX; ++i) {
        if (g_types[i].used && g_types[i].type_id == type_id) {
            if (size_out)
                *size_out = g_types[i].size;
            if (gc_bits_out)
                *gc_bits_out = g_types[i].gc_bits;
            return 1;
        }
    }
    return 0;
}

void gc_init(void) {
    g_n = 0;
    g_nroots = 0;
    g_nused = 0;
    g_req = 0;
    gc_type_clear();
}

void gc_request(void) {
    g_req = 1;
}

int gc_pending(void) {
    return g_req;
}

void gc_poll(void) {
    if (g_req)
        gc_collect();
}

void *gc_alloc(uint32_t type_id, uint32_t size) {
    uint8_t *p;
    int i;
    uint32_t tsz = 0;
    uint32_t bits = 0;
    if (size == 0)
        size = 8;
    if (gc_type_lookup(type_id, &tsz, &bits) && tsz > size)
        size = tsz;
    if (g_n == GC_MAX)
        gc_collect();
    if (g_n == GC_MAX)
        return 0;
    if (size <= 256u && g_nused + size <= GC_NURSERY) {
        p = g_nursery + g_nused;
        g_nused += size;
        g_obj[g_n].nurs = 1;
    } else {
        p = (uint8_t *)GCMALLOC(size);
        if (!p)
            return 0;
        g_obj[g_n].nurs = 0;
    }
    for (i = 0; i < (int)size; ++i)
        p[i] = 0;
    g_obj[g_n].type_id = type_id;
    g_obj[g_n].size = size;
    g_obj[g_n].marked = 0;
    g_obj[g_n].payload = p;
    return g_obj[g_n++].payload;
}

void gc_root_reg(void **slot) {
    if (g_nroots < 256)
        g_roots[g_nroots++] = slot;
}

static int is_ptr(uint8_t *p) {
    int i;
    for (i = 0; i < g_n; ++i) {
        if (g_obj[i].payload == p)
            return i;
    }
    return -1;
}

static void mark_obj(int i) {
    uint32_t bits = 0;
    uint32_t tsz = 0;
    uint32_t bit;
    if (i < 0 || g_obj[i].marked)
        return;
    g_obj[i].marked = 1;
    if (gc_type_lookup(g_obj[i].type_id, &tsz, &bits) && bits != 0) {
        for (bit = 0; bit < 32; ++bit) {
            uint32_t off;
            void *cand;
            int k;
            if (((bits >> bit) & 1u) == 0)
                continue;
            off = bit * (uint32_t)sizeof(void *);
            if (off + sizeof(void *) > g_obj[i].size)
                continue;
            cand = *(void **)(g_obj[i].payload + off);
            k = is_ptr((uint8_t *)cand);
            if (k >= 0)
                mark_obj(k);
        }
        return;
    }
    {
        uint32_t off;
        for (off = 0; off + sizeof(void *) <= g_obj[i].size;
             off += (uint32_t)sizeof(void *)) {
            void *cand;
            int k;
            cand = *(void **)(g_obj[i].payload + off);
            k = is_ptr((uint8_t *)cand);
            if (k >= 0)
                mark_obj(k);
        }
    }
}

void gc_collect(void) {
    int i;
    int w;
    g_req = 0;
    for (i = 0; i < g_n; ++i)
        g_obj[i].marked = 0;
    for (i = 0; i < g_nroots; ++i) {
        int k;
        if (!g_roots[i] || !*g_roots[i])
            continue;
        k = is_ptr((uint8_t *)*g_roots[i]);
        if (k >= 0)
            mark_obj(k);
    }
    w = 0;
    for (i = 0; i < g_n; ++i) {
        if (g_obj[i].marked) {
            if (g_obj[i].nurs) {
                uint8_t *old = g_obj[i].payload;
                uint8_t *p = (uint8_t *)GCMALLOC(g_obj[i].size);
                uint32_t b;
                int r;
                if (!p)
                    continue;
                for (b = 0; b < g_obj[i].size; ++b)
                    p[b] = old[b];
                for (r = 0; r < g_nroots; ++r) {
                    if (g_roots[r] && *g_roots[r] == (void *)old)
                        *g_roots[r] = p;
                }
                g_obj[i].payload = p;
                g_obj[i].nurs = 0;
            }
            g_obj[w++] = g_obj[i];
        } else if (!g_obj[i].nurs) {
            GCFREE(g_obj[i].payload);
        }
    }
    g_n = w;
    g_nused = 0;
}

uint32_t gc_live(void) {
    return (uint32_t)g_n;
}
