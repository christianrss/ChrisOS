#include <stdio.h>
#include <string.h>

#include "meminfo.h"

int main(void) {
    char text[256];
    int n = mem_format(text, sizeof text, 3u, 9u, 100u, 50u, 2);
    if (n < 0) {
        fprintf(stderr, "format failed\n");
        return 1;
    }
    if (strcmp(text,
               "pmm_used_pages 3\npmm_free_pages 9\n"
               "heap_used 100\nheap_free 50\ntasks 2\n") != 0) {
        fprintf(stderr, "meminfo text\n%s\n", text);
        return 1;
    }
    if (mem_format(text, 4, 1u, 1u, 1u, 1u, 1) != -1) {
        fprintf(stderr, "short buffer accepted\n");
        return 1;
    }
    printf("meminfo tests passed\n");
    return 0;
}
