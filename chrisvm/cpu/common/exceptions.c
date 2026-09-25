#include "machine/machine.h"

int chris_push8(ChrisCpu *cpu, uint64_t value) {
    uint64_t rsp = cpu->arch.rsp - 8ull;
    if (chris_va_write(cpu, rsp, &value, 8) != 0) {
        return -1;
    }
    cpu->arch.rsp = rsp;
    return 0;
}

int chris_pop8(ChrisCpu *cpu, uint64_t *value) {
    if (chris_va_read(cpu, cpu->arch.rsp, value, 8, 0) != 0) {
        return -1;
    }
    cpu->arch.rsp += 8ull;
    return 0;
}

static int read_desc(ChrisCpu *cpu, uint16_t sel, uint64_t *raw) {
    uint16_t index = (uint16_t)(sel >> 3);
    uint32_t off = (uint32_t)index * 8u;
    if ((sel & 0xfff8u) == 0) {
        return -1;
    }
    if ((uint32_t)cpu->arch.gdtr.limit + 1u < off + 8u) {
        return -1;
    }
    if (chris_va_read(cpu, cpu->arch.gdtr.base + off, raw, 8, 0) != 0) {
        return -2;
    }
    return 0;
}

int chris_seg_load_cs(ChrisCpu *cpu, uint16_t sel) {
    uint64_t raw = 0;
    int rc = read_desc(cpu, sel, &raw);
    if (rc != 0) {
        return rc;
    }
    if (((raw >> 47) & 1ull) == 0 || ((raw >> 43) & 1ull) == 0 || ((raw >> 53) & 1ull) == 0) {
        return -1;
    }
    cpu->arch.cs.sel = sel;
    cpu->arch.cs.base = 0;
    cpu->arch.cs.limit = 0xffffffffu;
    cpu->arch.cs.attr = (uint16_t)((raw >> 40) & 0xffffu);
    return 0;
}

/* 0 delivered, -1 report to the monitor (IDT not loaded), -2 delivery failed. */
static int deliver_frame(ChrisCpu *cpu, int vector, int has_error, uint32_t error) {
    uint8_t gate[16];
    uint64_t base;
    uint16_t sel;
    uint64_t offset;
    uint8_t type;
    uint64_t rsp_old;
    uint64_t frame_rip;
    if (cpu->arch.idtr.limit == 0 && cpu->arch.idtr.base == 0) {
        cpu->exit_reason = CHRIS_EXIT_EXCEPTION;
        cpu->ex_vector = vector;
        cpu->ex_error = error;
        cpu->halted = 1;
        return -1;
    }
    if ((uint32_t)cpu->arch.idtr.limit + 1u < (uint32_t)vector * 16u + 16u) {
        return -2;
    }
    base = cpu->arch.idtr.base + (uint64_t)vector * 16ull;
    if (chris_va_read(cpu, base, gate, 16, 0) != 0) {
        return -2;
    }
    type = (uint8_t)(gate[5] & 0x0fu);
    if ((gate[5] & 0x80u) == 0 || (type != 0x0e && type != 0x0f) || (gate[4] & 7) != 0) {
        return -2;
    }
    offset = (uint64_t)gate[0] | ((uint64_t)gate[1] << 8) | ((uint64_t)gate[6] << 16) |
             ((uint64_t)gate[7] << 24) | ((uint64_t)gate[8] << 32) | ((uint64_t)gate[9] << 40) |
             ((uint64_t)gate[10] << 48) | ((uint64_t)gate[11] << 56);
    sel = (uint16_t)(gate[2] | (gate[3] << 8));
    if (chris_seg_load_cs(cpu, sel) != 0) {
        return -2;
    }
    rsp_old = cpu->arch.rsp;
    frame_rip = cpu->arch.rip;
    if (chris_push8(cpu, cpu->arch.ss.sel) != 0 || chris_push8(cpu, rsp_old) != 0 ||
        chris_push8(cpu, cpu->arch.rflags) != 0 || chris_push8(cpu, sel) != 0 ||
        chris_push8(cpu, frame_rip) != 0) {
        return -2;
    }
    if (has_error && chris_push8(cpu, error) != 0) {
        return -2;
    }
    if (type == 0x0e) {
        cpu->arch.rflags &= ~(1ull << 9);
    }
    cpu->arch.rflags &= ~((1ull << 8) | (1ull << 16) | (1ull << 17));
    cpu->arch.rip = offset;
    cpu->rip_dirty = 1;
    return 0;
}

int chris_raise(ChrisCpu *cpu, int vector, int has_error, uint32_t error) {
    int stage;
    if (!cpu) {
        return -1;
    }
    cpu->ex_vector = vector;
    cpu->ex_error = error;
    for (stage = 1; stage <= 2; ++stage) {
        int rc;
        cpu->delivering = stage;
        rc = deliver_frame(cpu, vector, has_error, error);
        if (rc == 0) {
            cpu->delivering = 0;
            cpu->halted = 0;
            cpu->exit_reason = CHRIS_EXIT_NONE;
            return 0;
        }
        if (rc == -1) {
            cpu->delivering = 0;
            return -1;
        }
        vector = CHRIS_EX_DF;
        has_error = 1;
        error = 0;
    }
    cpu->delivering = 0;
    cpu->exit_reason = CHRIS_EXIT_TRIPLE;
    cpu->halted = 1;
    chris_trace_dump(cpu);
    return -1;
}
