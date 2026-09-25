#include "virtq.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "virtq: %s\n", msg);
    return 1;
}

int main(void) {
    uint8_t mem[4096];
    Virtq q;
    int i;
    uint16_t head;
    uint16_t second;
    if (virtq_bytes(3) != 0u) {
        return fail("non power size");
    }
    if (virtq_init(&q, mem, 4096u, 8) != 0) {
        return fail("init");
    }
    if (virtq_nfree(&q) != 8u) {
        return fail("nfree");
    }
    for (i = 0; i < 1000; ++i) {
        uint16_t id = 0;
        uint32_t len = 0;
        uint8_t *used;
        uint16_t slot;
        if (virtq_alloc(&q, 2, &head) != 0) {
            return fail("alloc");
        }
        second = virtq_next(&q, head);
        if (virtq_set(&q, head, 0x1000, 32, 0) != 0 ||
            virtq_set(&q, second, 0x2000, 64, VQ_DESC_F_WRITE) != 0 ||
            virtq_publish(&q, head) != 0) {
            return fail("publish");
        }
        used = mem + virtq_used_off(&q);
        slot = (uint16_t)(i % 8);
        used[4 + slot * 8] = (uint8_t)head;
        used[5 + slot * 8] = 0;
        used[6 + slot * 8] = 0;
        used[7 + slot * 8] = 0;
        used[8 + slot * 8] = 32;
        used[2] = (uint8_t)(i + 1);
        used[3] = (uint8_t)((i + 1) >> 8);
        if (virtq_take(&q, &id, &len) != 1 || id != head || len != 32u) {
            return fail("take");
        }
        virtq_reclaim(&q, head);
        if (virtq_nfree(&q) != 8u) {
            return fail("leak");
        }
    }
    if (virtq_alloc(&q, 2, &head) != 0) {
        return fail("zero cap followup");
    }
    virtq_reclaim(&q, head);
    if (virtq_alloc(&q, 9, &head) == 0) {
        return fail("overalloc");
    }
    printf("test_virtq: ok\n");
    return 0;
}
