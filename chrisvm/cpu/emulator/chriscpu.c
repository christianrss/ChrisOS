#include "machine/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fetch_insn(ChrisCpu *cpu, uint8_t *buf, int *got) {
    int i;
    *got = 0;
    for (i = 0; i < 15; ++i) {
        if (chris_va_read(cpu, cpu->arch.rip + (uint64_t)i, buf + i, 1, 2) != 0) {
            return -1;
        }
        *got = i + 1;
    }
    return 0;
}

static void maybe_irq(ChrisCpu *cpu) {
    if (cpu->sti_delay) {
        cpu->sti_delay = 0;
        return;
    }
    if (cpu->halted || !cpu->irq_pending) {
        return;
    }
    if ((cpu->arch.rflags & (1ull << 9)) == 0) {
        return;
    }
    cpu->irq_pending = 0;
    chris_raise(cpu, cpu->irq_vector, 0, 0);
}

static int cpu_run(ChrisCpu *cpu, uint64_t max_steps) {
    uint64_t n = 0;
    if (!cpu) {
        return -1;
    }
    cpu->halted = 0;
    if (cpu->exit_reason != CHRIS_EXIT_BREAK) {
        cpu->exit_reason = CHRIS_EXIT_NONE;
    }
    while (n < max_steps && !cpu->halted) {
        uint8_t raw[15];
        ChrisInsn insn;
        char text[96];
        int got = 0;
        int decoded;
        if (cpu->has_break && cpu->arch.rip == cpu->break_rip) {
            cpu->exit_reason = CHRIS_EXIT_BREAK;
            cpu->halted = 1;
            break;
        }
        if (fetch_insn(cpu, raw, &got) != 0) {
            if (cpu->exit_reason == CHRIS_EXIT_NONE) {
                cpu->exit_reason = CHRIS_EXIT_EXCEPTION;
                cpu->halted = 1;
            }
            break;
        }
        decoded = chris_decode(raw, got, &insn);
        if (decoded < 0) {
            chris_raise(cpu, CHRIS_EX_UD, 0, 0);
            break;
        }
        chris_format_insn(&insn, text, (int)sizeof text);
        if (cpu->trace) {
            char line[160];
            snprintf(line, sizeof line, "CPU0 #%llu RIP %016llx %s",
                     (unsigned long long)(cpu->steps + 1ull), (unsigned long long)cpu->arch.rip, text);
            chris_log(cpu->machine, line);
        }
        chris_trace_push(cpu, cpu->arch.rip, raw, insn.len, text);
        cpu->rip_dirty = 0;
        if (chris_execute(cpu, &insn) != 0 && cpu->exit_reason == CHRIS_EXIT_NONE && !cpu->halted) {
            cpu->exit_reason = CHRIS_EXIT_EXCEPTION;
            cpu->halted = 1;
        }
        if (!cpu->rip_dirty && !cpu->halted) {
            cpu->arch.rip += (uint64_t)insn.len;
        }
        maybe_irq(cpu);
        cpu->steps++;
        cpu->arch.tsc++;
        n++;
    }
    if (!cpu->halted) {
        cpu->exit_reason = CHRIS_EXIT_STEP_LIMIT;
    }
    return cpu->exit_reason;
}

static int cpu_init(ChrisMachine *machine) {
    (void)machine;
    return 0;
}

static int cpu_create(ChrisMachine *machine, unsigned cpu_id) {
    ChrisCpu *cpu;
    (void)cpu_id;
    if (!machine || machine->cpu) {
        return -1;
    }
    cpu = (ChrisCpu *)calloc(1, sizeof *cpu);
    if (!cpu) {
        return -1;
    }
    cpu->machine = machine;
    cpu->trace = machine->cfg.trace;
    cpu->trace_memory = machine->cfg.trace_memory;
    cpu->trace_io = machine->cfg.trace_io;
    cpu->trace_mmio = machine->cfg.trace_mmio;
    cpu->has_break = machine->cfg.has_break;
    cpu->break_rip = machine->cfg.break_rip;
    chris_arch_reset(&cpu->arch);
    machine->cpu = cpu;
    return 0;
}

static int cpu_reset(ChrisCpu *cpu) {
    uint64_t keep_steps = 0;
    ChrisMachine *m;
    if (!cpu) {
        return -1;
    }
    m = cpu->machine;
    chris_arch_reset(&cpu->arch);
    cpu->steps = keep_steps;
    cpu->halted = 0;
    cpu->exit_reason = CHRIS_EXIT_NONE;
    cpu->delivering = 0;
    cpu->irq_pending = 0;
    cpu->sti_delay = 0;
    (void)m;
    return 0;
}

static void cpu_inject(ChrisCpu *cpu, uint8_t vector) {
    if (!cpu) {
        return;
    }
    cpu->irq_pending = 1;
    cpu->irq_vector = vector;
}

static void cpu_get(const ChrisCpu *cpu, ChrisArchitectureState *out) {
    if (cpu && out) {
        *out = cpu->arch;
    }
}

static void cpu_set(ChrisCpu *cpu, const ChrisArchitectureState *in) {
    if (cpu && in) {
        cpu->arch = *in;
    }
}

static void cpu_tlb(ChrisCpu *cpu) {
    if (cpu) {
        cpu->tlb_gen++;
    }
}

static void cpu_shutdown(ChrisCpu *cpu) {
    if (!cpu) {
        return;
    }
    cpu->halted = 1;
    cpu->exit_reason = CHRIS_EXIT_SHUTDOWN;
}

const ChrisCpuBackend chriscpu_backend = {
    "chriscpu", cpu_init, cpu_create, cpu_reset, cpu_run, cpu_inject, cpu_get, cpu_set, cpu_tlb,
    cpu_shutdown,
};
