#include "chrisvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int want(const char *arg, const char *name) {
    size_t n = strlen(name);
    return strncmp(arg, name, n) == 0 && (arg[n] == 0 || arg[n] == '=');
}

int chris_config_from_args(ChrisConfig *cfg, int argc, char **argv, const char **image, char *err,
                           size_t errcap) {
    int i;
    if (image) {
        *image = 0;
    }
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--trace") == 0) {
            cfg->trace = 1;
        } else if (strcmp(a, "--trace-memory") == 0) {
            cfg->trace_memory = 1;
        } else if (strcmp(a, "--trace-io") == 0) {
            cfg->trace_io = 1;
        } else if (strcmp(a, "--trace-mmio") == 0) {
            cfg->trace_mmio = 1;
        } else if (strcmp(a, "--deterministic") == 0) {
            cfg->deterministic = 1;
        } else if (strcmp(a, "--debug") == 0) {
            cfg->debug = 1;
        } else if (strcmp(a, "--headless") == 0) {
            cfg->debug = 0;
        } else if (want(a, "--backend")) {
            const char *v = strchr(a, '=');
            cfg->backend = v ? v + 1 : (i + 1 < argc ? argv[++i] : "");
        } else if (want(a, "--break")) {
            const char *v = strchr(a, '=');
            if (!v) {
                snprintf(err, errcap, "missing break address");
                return -1;
            }
            cfg->break_rip = strtoull(v + 1, 0, 0);
            cfg->has_break = 1;
        } else if (want(a, "--max-steps")) {
            const char *v = strchr(a, '=');
            if (!v) {
                snprintf(err, errcap, "missing max-steps");
                return -1;
            }
            cfg->max_steps = strtoull(v + 1, 0, 0);
        } else if (a[0] == '-') {
            snprintf(err, errcap, "unknown option %s", a);
            return -1;
        } else if (image && !*image) {
            *image = a;
        } else {
            snprintf(err, errcap, "unexpected argument %s", a);
            return -1;
        }
    }
    if (image && !*image) {
        snprintf(err, errcap, "missing guest ELF");
        return -1;
    }
    return 0;
}
