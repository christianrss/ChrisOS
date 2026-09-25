#include "meminfo.h"

static int append_str(char *dst, uint32_t cap, uint32_t *used, const char *text) {
    while (text && *text) {
        if (*used + 1u >= cap) {
            return -1;
        }
        dst[(*used)++] = *text++;
    }
    return 0;
}

static int append_u64(char *dst, uint32_t cap, uint32_t *used, uint64_t value) {
    char rev[20];
    unsigned n = 0u;
    if (value == 0u) {
        return append_str(dst, cap, used, "0");
    }
    while (value != 0u && n < 20u) {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) {
        char one[2];
        one[0] = rev[--n];
        one[1] = 0;
        if (append_str(dst, cap, used, one) != 0) {
            return -1;
        }
    }
    return 0;
}

int mem_format(char *dst, uint32_t cap, uint64_t pmm_used_pages,
               uint64_t pmm_free_pages, uint64_t heap_used,
               uint64_t heap_free, int tasks) {
    uint32_t used = 0u;
    uint64_t task_n;

    if (!dst || cap < 2u) {
        return -1;
    }
    task_n = tasks < 0 ? 0u : (uint64_t)tasks;
    if (append_str(dst, cap, &used, "pmm_used_pages ") != 0 ||
        append_u64(dst, cap, &used, pmm_used_pages) != 0 ||
        append_str(dst, cap, &used, "\npmm_free_pages ") != 0 ||
        append_u64(dst, cap, &used, pmm_free_pages) != 0 ||
        append_str(dst, cap, &used, "\nheap_used ") != 0 ||
        append_u64(dst, cap, &used, heap_used) != 0 ||
        append_str(dst, cap, &used, "\nheap_free ") != 0 ||
        append_u64(dst, cap, &used, heap_free) != 0 ||
        append_str(dst, cap, &used, "\ntasks ") != 0 ||
        append_u64(dst, cap, &used, task_n) != 0 ||
        append_str(dst, cap, &used, "\n") != 0) {
        return -1;
    }
    dst[used] = 0;
    return (int)used;
}
