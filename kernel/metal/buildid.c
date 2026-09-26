#include "buildid.h"
#include "serial.h"

#ifndef CHRIS_GIT
#define CHRIS_GIT "unknown"
#endif
#ifndef CHRIS_BUILD_ID
#define CHRIS_BUILD_ID "unknown"
#endif
#ifndef CHRIS_DATE
#define CHRIS_DATE "unknown"
#endif

/* The stamp tool finds this tag and replaces the 64 zeros with the hex
 * SHA-256 of the linked image while those digits are still zero. */
char g_build_identity[] __attribute__((used)) =
    "CHRISOSHASH:0000000000000000000000000000000000000000000000000000000000000000";

const char *build_git(void) {
    return CHRIS_GIT;
}

const char *build_id(void) {
    return CHRIS_BUILD_ID;
}

const char *build_date(void) {
    return CHRIS_DATE;
}

const char *build_compiler(void) {
    return __VERSION__;
}

const char *build_kernel_sha256(void) {
    return g_build_identity + 12;
}

static int append(char *dst, uint32_t cap, uint32_t *used, const char *text) {
    if (!text) {
        return 0;
    }
    while (*text) {
        if (*used + 1u >= cap) {
            return -1;
        }
        dst[*used] = *text;
        (*used)++;
        text++;
    }
    return 0;
}

int build_info_format(char *dst, uint32_t cap) {
    uint32_t used = 0u;

    if (!dst || cap < 2u) {
        return -1;
    }
    if (append(dst, cap, &used, "ChrisOS\nBuild ID: ") != 0 ||
        append(dst, cap, &used, build_id()) != 0 ||
        append(dst, cap, &used, "\nGit: ") != 0 ||
        append(dst, cap, &used, build_git()) != 0 ||
        append(dst, cap, &used, "\nDate: ") != 0 ||
        append(dst, cap, &used, build_date()) != 0 ||
        append(dst, cap, &used, "\nCompiler: ") != 0 ||
        append(dst, cap, &used, build_compiler()) != 0 ||
        append(dst, cap, &used, "\nKernel SHA256: ") != 0 ||
        append(dst, cap, &used, build_kernel_sha256()) != 0 ||
        append(dst, cap, &used, "\n") != 0) {
        return -1;
    }
    dst[used] = 0;
    return (int)used;
}

void build_info_log(void) {
    char text[512];
    if (build_info_format(text, sizeof text) < 0) {
        serial_puts("ChrisOS build id truncated\n");
        return;
    }
    serial_puts(text);
}
