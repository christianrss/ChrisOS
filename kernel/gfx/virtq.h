#ifndef CHRIS_VIRTQ_H
#define CHRIS_VIRTQ_H

#include <stdint.h>

#define VQ_DESC_F_NEXT 1u
#define VQ_DESC_F_WRITE 2u
#define VQ_MAX 128

typedef struct Virtq {
    uint8_t *mem;
    uint32_t mem_bytes;
    uint16_t qsz;
    uint16_t free_head;
    uint16_t nfree;
    uint16_t last_used;
    uint16_t link_next[VQ_MAX];
    uint16_t link_flags[VQ_MAX];
    uint32_t desc_off;
    uint32_t avail_off;
    uint32_t used_off;
} Virtq;

uint32_t virtq_bytes(uint16_t qsz);
int virtq_init(Virtq *q, uint8_t *mem, uint32_t mem_bytes, uint16_t qsz);
uint32_t virtq_avail_off(const Virtq *q);
uint32_t virtq_used_off(const Virtq *q);
uint16_t virtq_nfree(const Virtq *q);
int virtq_alloc(Virtq *q, uint16_t n, uint16_t *head);
int virtq_set(Virtq *q, uint16_t idx, uint64_t addr, uint32_t len, uint16_t flags);
uint16_t virtq_next(const Virtq *q, uint16_t idx);
int virtq_publish(Virtq *q, uint16_t head);
int virtq_take(Virtq *q, uint16_t *id_out, uint32_t *len_out);
void virtq_reclaim(Virtq *q, uint16_t head);

#endif
