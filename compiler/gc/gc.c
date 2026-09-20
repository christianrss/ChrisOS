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

static GcObj g_obj[GC_MAX];
static int g_n;
static void **g_roots[256];
static int g_nroots;
static uint8_t g_nursery[GC_NURSERY];
static uint32_t g_nused;
static int g_req;

void gc_collect(void);

void gc_init(void) {
    g_n = 0;
    g_nroots = 0;
    g_nused = 0;
    g_req = 0;
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
    if (size == 0)
        size = 8;
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
    uint32_t off;
    if (i < 0 || g_obj[i].marked)
        return;
    g_obj[i].marked = 1;
    for (off = 0; off + sizeof(void *) <= g_obj[i].size; off += sizeof(void *)) {
        void *cand;
        int k;
        cand = *(void **)(g_obj[i].payload + off);
        k = is_ptr((uint8_t *)cand);
        if (k >= 0)
            mark_obj(k);
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
