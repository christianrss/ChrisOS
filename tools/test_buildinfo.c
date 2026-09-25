#include <stdio.h>
#include <string.h>

#include "buildid.h"

void serial_puts(const char *text) {
    (void)text;
}

static int contains(const char *text, const char *needle) {
    return strstr(text, needle) != 0;
}

int main(void) {
    char text[512];
    int n;
    const char *hash;

    n = build_info_format(text, sizeof text);
    if (n < 0) {
        fprintf(stderr, "format failed\n");
        return 1;
    }
    if (!contains(text, "Build ID: abc-2026") ||
        !contains(text, "Git: abc") ||
        !contains(text, "Date: 2026-09-25") ||
        !contains(text, "Compiler: ") ||
        !contains(text, "Kernel SHA256: ")) {
        fprintf(stderr, "missing field\n%s\n", text);
        return 1;
    }
    hash = build_kernel_sha256();
    if (strlen(hash) != 64u) {
        fprintf(stderr, "hash width\n");
        return 1;
    }
    if (build_info_format(text, 8) != -1) {
        fprintf(stderr, "short buffer accepted\n");
        return 1;
    }
    printf("build info tests passed\n");
    return 0;
}
