#include "machine/machine.h"

#include <stdlib.h>
#include <string.h>

void chris_config_init(ChrisConfig *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->ram_size = CHRIS_RAM_DEFAULT;
    cfg->backend = "chriscpu";
    cfg->deterministic = 1;
    cfg->max_steps = 1000000ull;
}

const ChrisCpuBackend *chris_backend_by_name(const char *name) {
    if (!name || strcmp(name, "chriscpu") == 0) {
        return &chriscpu_backend;
    }
    if (strcmp(name, "chrishv") == 0) {
        return &chrishv_backend;
    }
    return 0;
}

ChrisMachine *chris_machine_create(const ChrisConfig *cfg) {
    ChrisMachine *m;
    ChrisConfig local;
    if (!cfg) {
        chris_config_init(&local);
        cfg = &local;
    }
    if (cfg->ram_size < 2ull * 1024ull * 1024ull || (cfg->ram_size & 0x1fffffull) != 0) {
        return 0;
    }
    m = (ChrisMachine *)calloc(1, sizeof *m);
    if (!m) {
        return 0;
    }
    m->cfg = *cfg;
    if (!m->cfg.backend) {
        m->cfg.backend = "chriscpu";
    }
    m->backend = chris_backend_by_name(m->cfg.backend);
    if (!m->backend || m->backend->init(m) != 0) {
        free(m);
        return 0;
    }
    m->ram = (uint8_t *)calloc(1, (size_t)cfg->ram_size);
    m->ram_size = cfg->ram_size;
    if (!m->ram) {
        free(m);
        return 0;
    }
    chris_serial_attach(m);
    if (chris_fb_attach(m) != 0) {
        free(m->ram);
        free(m);
        return 0;
    }
    if (m->backend->create_cpu(m, 0) != 0) {
        free(m->fb.pix);
        free(m->ram);
        free(m);
        return 0;
    }
    return m;
}

void chris_machine_destroy(ChrisMachine *m) {
    if (!m) {
        return;
    }
    if (m->cpu && m->backend) {
        m->backend->shutdown(m->cpu);
    }
    free(m->cpu);
    free(m->fb.pix);
    free(m->ram);
    free(m);
}

int chris_write_ram(ChrisMachine *m, uint64_t pa, const void *src, size_t n) {
    return chris_phys_write(m, pa, src, n);
}

int chris_read_ram(ChrisMachine *m, uint64_t pa, void *dst, size_t n) {
    return chris_phys_read(m, pa, dst, n);
}

static ChrisCpu *need(const ChrisMachine *m) {
    return m ? m->cpu : 0;
}

void chris_set_gpr(ChrisMachine *m, int index, uint64_t value) {
    if (need(m) && index >= 0 && index < 16) {
        m->cpu->arch.gpr[index] = value;
    }
}

uint64_t chris_get_gpr(const ChrisMachine *m, int index) {
    if (!need(m) || index < 0 || index > 15) {
        return 0;
    }
    return m->cpu->arch.gpr[index];
}

uint64_t chris_get_rip(const ChrisMachine *m) {
    return need(m) ? m->cpu->arch.rip : 0;
}

uint64_t chris_get_rflags(const ChrisMachine *m) {
    return need(m) ? m->cpu->arch.rflags : 0;
}

uint64_t chris_get_cr(const ChrisMachine *m, int n) {
    if (!need(m)) {
        return 0;
    }
    if (n == 0) {
        return m->cpu->arch.cr0;
    }
    if (n == 2) {
        return m->cpu->arch.cr2;
    }
    if (n == 3) {
        return m->cpu->arch.cr3;
    }
    if (n == 4) {
        return m->cpu->arch.cr4;
    }
    if (n == 8) {
        return m->cpu->arch.cr8;
    }
    return 0;
}

void chris_set_cr(ChrisMachine *m, int n, uint64_t value) {
    if (!need(m)) {
        return;
    }
    if (n == 0) {
        m->cpu->arch.cr0 = value;
    } else if (n == 2) {
        m->cpu->arch.cr2 = value;
    } else if (n == 3) {
        m->cpu->arch.cr3 = value;
    } else if (n == 4) {
        m->cpu->arch.cr4 = value;
    } else if (n == 8) {
        m->cpu->arch.cr8 = value;
    }
}

int chris_exit_reason(const ChrisMachine *m) {
    return need(m) ? m->cpu->exit_reason : -1;
}

int chris_exception_vector(const ChrisMachine *m) {
    return need(m) ? m->cpu->ex_vector : -1;
}

uint64_t chris_steps(const ChrisMachine *m) {
    return need(m) ? m->cpu->steps : 0;
}

int chris_run(ChrisMachine *m, uint64_t max_steps) {
    if (!m || !m->cpu || !m->booted) {
        return -1;
    }
    if (m->shutdown) {
        return CHRIS_EXIT_SHUTDOWN;
    }
    return m->backend->run(m->cpu, max_steps);
}

const char *chris_serial_text(const ChrisMachine *m, size_t *len) {
    if (!m) {
        return "";
    }
    if (len) {
        *len = m->serial.tx_len;
    }
    return m->serial.tx;
}

void chris_serial_set_hook(ChrisMachine *m, void (*hook)(void *ctx, char ch), void *ctx) {
    if (!m) {
        return;
    }
    m->serial.hook = hook;
    m->serial.hook_ctx = ctx;
}

void chris_set_log(ChrisMachine *m, void (*log)(void *ctx, const char *line), void *ctx) {
    if (!m) {
        return;
    }
    m->log = log;
    m->log_ctx = ctx;
}
