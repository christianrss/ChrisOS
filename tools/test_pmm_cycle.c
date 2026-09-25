#include <stdio.h>
#include <stdint.h>

#include "pmm.h"

int main(void) {
    uint64_t before;
    uint64_t pages[1000];
    int i;

    pmm_init();
    before = pmm_free_pages();
    if (before < 1000u) {
        fprintf(stderr, "not enough pages %llu\n", (unsigned long long)before);
        return 1;
    }
    for (i = 0; i < 1000; i++) {
        pages[i] = pmm_alloc();
        if (pages[i] == 0u) {
            fprintf(stderr, "alloc %d\n", i);
            return 1;
        }
    }
    if (pmm_free_pages() != before - 1000u) {
        fprintf(stderr, "used count\n");
        return 1;
    }
    for (i = 0; i < 1000; i++) {
        pmm_free(pages[i]);
    }
    if (pmm_free_pages() != before || pmm_used_pages() + pmm_free_pages() != pmm_usable_pages()) {
        fprintf(stderr, "free count after cycle\n");
        return 1;
    }
    printf("pmm cycle tests passed\n");
    return 0;
}
