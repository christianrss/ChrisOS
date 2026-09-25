#include "machine/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_line(void *ctx, const char *line) {
    (void)ctx;
    fprintf(stderr, "%s\n", line);
}

static void tx_hook(void *ctx, char ch) {
    (void)ctx;
    fputc(ch, stdout);
    fflush(stdout);
}

static int load_file(const char *path, uint8_t **out, size_t *n) {
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *n = (size_t)sz;
    return 0;
}

static void debug_loop(ChrisMachine *m, uint64_t max_steps) {
    char line[128];
    fprintf(stderr, "chrisvm debug: s step, c continue, r regs, q quit, b HEX\n");
    chris_dump_cpu(m);
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'q') {
            return;
        }
        if (line[0] == 'r') {
            chris_dump_cpu(m);
            continue;
        }
        if (line[0] == 'b') {
            m->cpu->break_rip = strtoull(line + 1, 0, 0);
            m->cpu->has_break = 1;
            continue;
        }
        if (line[0] == 's' || line[0] == 'c') {
            int reason;
            if (line[0] == 'c') {
                m->cpu->has_break = 0;
            }
            m->cpu->halted = 0;
            reason = chris_run(m, line[0] == 's' ? 1ull : max_steps);
            chris_dump_cpu(m);
            if (reason == CHRIS_EXIT_HLT || reason == CHRIS_EXIT_SHUTDOWN || reason == CHRIS_EXIT_TRIPLE ||
                reason == CHRIS_EXIT_EXCEPTION) {
                return;
            }
        }
    }
}

int main(int argc, char **argv) {
    ChrisConfig cfg;
    const char *image = 0;
    char err[160];
    uint8_t *buf = 0;
    size_t n = 0;
    ChrisMachine *m;
    int reason;
    int rc = 1;
    chris_config_init(&cfg);
    err[0] = 0;
    if (chris_config_from_args(&cfg, argc, argv, &image, err, sizeof err) != 0) {
        fprintf(stderr, "chrisvm: %s\n", err[0] ? err : "usage");
        fprintf(stderr, "usage: chrisvm [--backend=chriscpu] [--trace] [--debug] [--headless] guest.elf\n");
        return 2;
    }
    if (load_file(image, &buf, &n) != 0) {
        fprintf(stderr, "chrisvm: cannot read %s\n", image);
        return 2;
    }
    m = chris_machine_create(&cfg);
    if (!m) {
        fprintf(stderr, "chrisvm: machine create failed\n");
        free(buf);
        return 2;
    }
    chris_set_log(m, log_line, 0);
    chris_serial_set_hook(m, tx_hook, 0);
    if (chris_load_elf(m, buf, n, err, sizeof err) != 0 || chris_boot(m, 0, CHRIS_STACK_RSP) != 0) {
        fprintf(stderr, "chrisvm: boot failed: %s\n", err[0] ? err : "entry");
        chris_machine_destroy(m);
        free(buf);
        return 2;
    }
    free(buf);
    if (cfg.debug) {
        debug_loop(m, cfg.max_steps ? cfg.max_steps : 1000000ull);
        reason = chris_exit_reason(m);
    } else {
        reason = chris_run(m, cfg.max_steps ? cfg.max_steps : 1000000ull);
    }
    fprintf(stderr, "chrisvm: exit %d steps %llu rip %016llx rax %016llx\n", reason,
            (unsigned long long)chris_steps(m), (unsigned long long)chris_get_rip(m),
            (unsigned long long)chris_get_gpr(m, 0));
    if (reason == CHRIS_EXIT_HLT || reason == CHRIS_EXIT_SHUTDOWN) {
        rc = 0;
    }
    chris_machine_destroy(m);
    return rc;
}
