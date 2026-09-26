#include "machine/machine.h"

#include <stdio.h>
#include <string.h>

void chris_log(ChrisMachine *m, const char *line) {
    if (!m || !line) {
        return;
    }
    if (m->log) {
        m->log(m->log_ctx, line);
    }
}

void chris_trace_push(ChrisCpu *cpu, uint64_t rip, const uint8_t *bytes, int len, const char *text) {
    ChrisTraceEnt *e;
    if (!cpu) {
        return;
    }
    e = &cpu->ring[cpu->ring_i % CHRIS_TRACE_RING];
    e->rip = rip;
    e->len = len > 15 ? 15 : len;
    if (bytes && e->len > 0) {
        memcpy(e->bytes, bytes, (size_t)e->len);
    }
    e->text[0] = 0;
    if (text) {
        snprintf(e->text, sizeof e->text, "%s", text);
    }
    cpu->ring_i++;
    if (cpu->ring_n < CHRIS_TRACE_RING) {
        cpu->ring_n++;
    }
}

void chris_trace_dump(const ChrisCpu *cpu) {
    int count;
    int start;
    int i;
    if (!cpu || !cpu->machine) {
        return;
    }
    count = cpu->ring_n;
    start = cpu->ring_i - count;
    chris_log(cpu->machine, "recent instructions:");
    for (i = 0; i < count; ++i) {
        const ChrisTraceEnt *e = &cpu->ring[(start + i) & (CHRIS_TRACE_RING - 1)];
        char line[180];
        snprintf(line, sizeof line, "  %016llx %s", (unsigned long long)e->rip, e->text);
        chris_log(cpu->machine, line);
    }
}

void chris_dump_cpu(const ChrisMachine *m) {
    const ChrisCpu *cpu;
    char line[160];
    static const char *name[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
    int i;
    if (!m || !m->cpu) {
        return;
    }
    cpu = m->cpu;
    snprintf(line, sizeof line, "RIP %016llx RSP %016llx RFLAGS %016llx",
             (unsigned long long)cpu->arch.rip, (unsigned long long)cpu->arch.gpr[4],
             (unsigned long long)cpu->arch.rflags);
    chris_log((ChrisMachine *)m, line);
    for (i = 0; i < 8; ++i) {
        snprintf(line, sizeof line, "%s %016llx", name[i], (unsigned long long)cpu->arch.gpr[i]);
        chris_log((ChrisMachine *)m, line);
    }
    snprintf(line, sizeof line, "CR0 %016llx CR2 %016llx CR3 %016llx CR4 %016llx",
             (unsigned long long)cpu->arch.cr0, (unsigned long long)cpu->arch.cr2,
             (unsigned long long)cpu->arch.cr3, (unsigned long long)cpu->arch.cr4);
    chris_log((ChrisMachine *)m, line);
}
