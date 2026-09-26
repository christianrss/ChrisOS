#include "machine/machine.h"

#include <string.h>

static uint64_t size_mask(int os) {
    if (os == 1) {
        return 0xffull;
    }
    if (os == 2) {
        return 0xffffull;
    }
    if (os == 4) {
        return 0xffffffffull;
    }
    return ~0ull;
}

uint64_t chris_imm_sx(const ChrisInsn *in) {
    if (in->imm_bytes == 1) {
        return (uint64_t)(int64_t)(int8_t)in->imm;
    }
    if (in->imm_bytes == 2) {
        return (uint64_t)(int64_t)(int16_t)in->imm;
    }
    if (in->imm_bytes == 4) {
        return (uint64_t)(int64_t)(int32_t)in->imm;
    }
    return in->imm;
}

static int high8(const ChrisInsn *in, int reg) {
    return in->os == 1 && !in->rex && reg >= 4 && reg <= 7;
}

static int gpr_index8(const ChrisInsn *in, int reg) {
    if (high8(in, reg)) {
        return reg - 4;
    }
    return reg;
}

int chris_read_gpr(const ChrisCpu *cpu, const ChrisInsn *in, int reg, int os, uint64_t *out) {
    uint64_t v;
    if (reg < 0 || reg > 15) {
        return -1;
    }
    if (os == 1) {
        int idx = gpr_index8(in, reg);
        v = cpu->arch.gpr[idx];
        if (high8(in, reg)) {
            v >>= 8;
        }
        *out = v & 0xffull;
        return 0;
    }
    *out = cpu->arch.gpr[reg] & size_mask(os);
    return 0;
}

int chris_write_gpr(ChrisCpu *cpu, const ChrisInsn *in, int reg, int os, uint64_t value) {
    if (reg < 0 || reg > 15) {
        return -1;
    }
    if (os == 1) {
        int idx = gpr_index8(in, reg);
        uint64_t cur = cpu->arch.gpr[idx];
        if (high8(in, reg)) {
            cur = (cur & ~0xff00ull) | ((value & 0xffull) << 8);
        } else {
            cur = (cur & ~0xffull) | (value & 0xffull);
        }
        cpu->arch.gpr[idx] = cur;
        return 0;
    }
    if (os == 2) {
        cpu->arch.gpr[reg] = (cpu->arch.gpr[reg] & ~0xffffull) | (value & 0xffffull);
        return 0;
    }
    if (os == 4) {
        cpu->arch.gpr[reg] = value & 0xffffffffull;
        return 0;
    }
    cpu->arch.gpr[reg] = value;
    return 0;
}

int chris_eff_addr(const ChrisCpu *cpu, const ChrisInsn *in, uint64_t *ea) {
    uint64_t addr = 0;
    if (!in->has_modrm || in->mod == 3) {
        return -1;
    }
    if (in->rip_rel) {
        addr = cpu->arch.rip + (uint64_t)in->len + (uint64_t)in->disp;
    } else {
        if (!in->no_base) {
            int base = in->has_sib ? in->base : in->rm;
            addr += cpu->arch.gpr[base & 15];
        }
        if (in->has_sib && !in->no_index) {
            addr += cpu->arch.gpr[in->index & 15] << in->scale;
        }
        if (in->has_disp) {
            addr += (uint64_t)in->disp;
        }
    }
    if (in->asz == 4) {
        addr &= 0xffffffffull;
    }
    *ea = addr;
    return 0;
}

int chris_read_rm(ChrisCpu *cpu, const ChrisInsn *in, uint64_t *out) {
    if (in->mod == 3 || !in->has_modrm) {
        return chris_read_gpr(cpu, in, in->has_modrm ? in->rm : 0, in->os, out);
    }
    {
        uint64_t ea = 0;
        uint64_t v = 0;
        uint8_t buf[8];
        int n = in->os;
        memset(buf, 0, sizeof buf);
        if (chris_eff_addr(cpu, in, &ea) != 0) {
            return -1;
        }
        if (chris_va_read(cpu, ea, buf, (size_t)n, 0) != 0) {
            return -1;
        }
        memcpy(&v, buf, (size_t)n);
        *out = v;
        return 0;
    }
}

int chris_write_rm(ChrisCpu *cpu, const ChrisInsn *in, uint64_t value) {
    if (in->mod == 3 || !in->has_modrm) {
        return chris_write_gpr(cpu, in, in->has_modrm ? in->rm : 0, in->os, value);
    }
    {
        uint64_t ea = 0;
        uint8_t buf[8];
        int n = in->os;
        if (chris_eff_addr(cpu, in, &ea) != 0) {
            return -1;
        }
        memset(buf, 0, sizeof buf);
        memcpy(buf, &value, (size_t)n);
        return chris_va_write(cpu, ea, buf, (size_t)n);
    }
}

int chris_read_regop(const ChrisCpu *cpu, const ChrisInsn *in, uint64_t *out) {
    return chris_read_gpr(cpu, in, in->reg, in->os, out);
}

int chris_write_regop(ChrisCpu *cpu, const ChrisInsn *in, uint64_t value) {
    return chris_write_gpr(cpu, in, in->reg, in->os, value);
}
