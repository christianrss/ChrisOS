#include "virtq.h"

static void vq_mb(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("mfence" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

static uint32_t align4(uint32_t n) {
    return (n + 3u) & ~3u;
}

uint32_t virtq_bytes(uint16_t qsz) {
    uint32_t desc;
    uint32_t avail;
    uint32_t used_at;
    uint32_t used;
    if (qsz < 2u || qsz > VQ_MAX || (qsz & (uint16_t)(qsz - 1u)) != 0u) {
        return 0;
    }
    desc = (uint32_t)qsz * 16u;
    avail = 4u + 2u * (uint32_t)qsz + 2u;
    used_at = align4(desc + avail);
    used = 4u + 8u * (uint32_t)qsz + 2u;
    return used_at + used;
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

uint32_t virtq_avail_off(const Virtq *q) {
    return q->avail_off;
}

uint32_t virtq_used_off(const Virtq *q) {
    return q->used_off;
}

uint16_t virtq_nfree(const Virtq *q) {
    return q->nfree;
}

int virtq_init(Virtq *q, uint8_t *mem, uint32_t mem_bytes, uint16_t qsz) {
    uint32_t need;
    uint32_t i;
    uint16_t n;
    if (!q || !mem) {
        return -1;
    }
    need = virtq_bytes(qsz);
    if (need == 0u || need > mem_bytes) {
        return -1;
    }
    for (i = 0; i < need; ++i) {
        mem[i] = 0;
    }
    q->mem = mem;
    q->mem_bytes = mem_bytes;
    q->qsz = qsz;
    q->desc_off = 0;
    q->avail_off = (uint32_t)qsz * 16u;
    q->used_off = align4(q->avail_off + 4u + 2u * (uint32_t)qsz + 2u);
    q->free_head = 0;
    q->nfree = qsz;
    q->last_used = 0;
    for (n = 0; n < qsz; ++n) {
        q->link_next[n] = (uint16_t)(n + 1u);
        q->link_flags[n] = 0;
    }
    q->link_next[qsz - 1u] = 0xffffu;
    return 0;
}

int virtq_alloc(Virtq *q, uint16_t n, uint16_t *head) {
    uint16_t i;
    uint16_t cur;
    uint16_t prev;
    if (!q || !head || n == 0u || n > q->nfree) {
        return -1;
    }
    *head = q->free_head;
    prev = 0;
    cur = q->free_head;
    for (i = 0; i < n; ++i) {
        uint16_t nxt = q->link_next[cur];
        q->link_flags[cur] = (i + 1u < n) ? VQ_DESC_F_NEXT : 0;
        q->link_next[cur] = (i + 1u < n) ? nxt : 0;
        prev = cur;
        cur = nxt;
        (void)prev;
    }
    q->free_head = cur;
    q->nfree = (uint16_t)(q->nfree - n);
    return 0;
}

uint16_t virtq_next(const Virtq *q, uint16_t idx) {
    if (!q || idx >= q->qsz) {
        return 0xffffu;
    }
    return q->link_next[idx];
}

int virtq_set(Virtq *q, uint16_t idx, uint64_t addr, uint32_t len, uint16_t flags) {
    uint8_t *d;
    uint16_t next;
    if (!q || idx >= q->qsz) {
        return -1;
    }
    next = q->link_next[idx];
    q->link_flags[idx] = (uint16_t)(q->link_flags[idx] | flags);
    d = q->mem + q->desc_off + (uint32_t)idx * 16u;
    wr64(d, addr);
    wr32(d + 8, len);
    wr16(d + 12, q->link_flags[idx]);
    wr16(d + 14, next);
    return 0;
}

int virtq_publish(Virtq *q, uint16_t head) {
    uint8_t *avail;
    uint16_t idx;
    uint16_t slot;
    if (!q || head >= q->qsz) {
        return -1;
    }
    vq_mb();
    avail = q->mem + q->avail_off;
    idx = rd16(avail + 2);
    slot = (uint16_t)(idx % q->qsz);
    wr16(avail + 4u + (uint32_t)slot * 2u, head);
    vq_mb();
    wr16(avail + 2, (uint16_t)(idx + 1u));
    vq_mb();
    return 0;
}

int virtq_take(Virtq *q, uint16_t *id_out, uint32_t *len_out) {
    uint8_t *used;
    uint16_t idx;
    uint16_t slot;
    uint8_t *elem;
    if (!q || !id_out || !len_out) {
        return -1;
    }
    used = q->mem + q->used_off;
    vq_mb();
    idx = rd16(used + 2);
    if (idx == q->last_used) {
        return 0;
    }
    slot = (uint16_t)(q->last_used % q->qsz);
    elem = used + 4u + (uint32_t)slot * 8u;
    {
        uint32_t id32 = (uint32_t)elem[0] | ((uint32_t)elem[1] << 8) |
                        ((uint32_t)elem[2] << 16) | ((uint32_t)elem[3] << 24);
        if (id32 >= q->qsz) {
            return -1;
        }
        *id_out = (uint16_t)id32;
    }
    *len_out = (uint32_t)elem[4] | ((uint32_t)elem[5] << 8) |
               ((uint32_t)elem[6] << 16) | ((uint32_t)elem[7] << 24);
    q->last_used = (uint16_t)(q->last_used + 1u);
    return 1;
}

void virtq_reclaim(Virtq *q, uint16_t head) {
    uint16_t cur;
    if (!q || head >= q->qsz) {
        return;
    }
    cur = head;
    for (;;) {
        uint16_t nxt = q->link_next[cur];
        uint16_t flags = q->link_flags[cur];
        q->link_flags[cur] = 0;
        q->link_next[cur] = q->free_head;
        q->free_head = cur;
        q->nfree = (uint16_t)(q->nfree + 1u);
        if ((flags & VQ_DESC_F_NEXT) == 0u) {
            break;
        }
        cur = nxt;
        if (cur >= q->qsz) {
            break;
        }
    }
}
