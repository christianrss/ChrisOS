#include "machine/machine.h"

#include <string.h>

static int fail_ud(ChrisCpu *cpu) {
    return chris_raise(cpu, CHRIS_EX_UD, 0, 0);
}

static int lock_ok(ChrisCpu *cpu, const ChrisInsn *in) {
    if (in->lock && (in->mod == 3 || !in->has_modrm)) {
        return fail_ud(cpu);
    }
    return 0;
}

static uint64_t sx_to(uint64_t v, int src_os) {
    if (src_os == 1) {
        return (uint64_t)(int64_t)(int8_t)v;
    }
    if (src_os == 2) {
        return (uint64_t)(int64_t)(int16_t)v;
    }
    if (src_os == 4) {
        return (uint64_t)(int64_t)(int32_t)v;
    }
    return v;
}

static int do_alu(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t r = 0;
    int write = in->alu != CHRIS_ALU_CMP && in->alu != CHRIS_ALU_TEST;
    if (lock_ok(cpu, in) != 0) {
        return -1;
    }
    if (in->form == CHRIS_FORM_RM_REG) {
        if (chris_read_rm(cpu, in, &a) != 0 || chris_read_regop(cpu, in, &b) != 0) {
            return -1;
        }
    } else if (in->form == CHRIS_FORM_REG_RM) {
        if (chris_read_regop(cpu, in, &a) != 0 || chris_read_rm(cpu, in, &b) != 0) {
            return -1;
        }
    } else if (in->form == CHRIS_FORM_ACC_IMM) {
        if (chris_read_gpr(cpu, in, 0, in->os, &a) != 0) {
            return -1;
        }
        b = chris_imm_sx(in);
    } else {
        if (chris_read_rm(cpu, in, &a) != 0) {
            return -1;
        }
        b = chris_imm_sx(in);
    }
    if (cpu->halted) {
        return -1;
    }
    cpu->arch.rflags = chris_flags_bin(in->alu, a, b, in->os, cpu->arch.rflags, &r);
    if (!write) {
        return 0;
    }
    if (in->form == CHRIS_FORM_REG_RM) {
        return chris_write_regop(cpu, in, r);
    }
    if (in->form == CHRIS_FORM_ACC_IMM) {
        return chris_write_gpr(cpu, in, 0, in->os, r);
    }
    return chris_write_rm(cpu, in, r);
}

static int do_mov(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    if (in->reg_only_push) {
        v = in->os == 8 && in->imm_bytes == 4 ? chris_imm_sx(in) : in->imm;
        if (in->os == 8 && in->imm_bytes == 8) {
            v = in->imm;
        }
        return chris_write_gpr(cpu, in, in->rm, in->os, v);
    }
    if (in->form == CHRIS_FORM_RM_IMM) {
        v = in->os == 8 ? chris_imm_sx(in) : in->imm;
        return chris_write_rm(cpu, in, v);
    }
    if (in->form == CHRIS_FORM_RM_REG) {
        if (chris_read_regop(cpu, in, &v) != 0) {
            return -1;
        }
        return chris_write_rm(cpu, in, v);
    }
    if (chris_read_rm(cpu, in, &v) != 0) {
        return -1;
    }
    return chris_write_regop(cpu, in, v);
}

static int do_extend(ChrisCpu *cpu, const ChrisInsn *in, int sign) {
    uint64_t v = 0;
    ChrisInsn src = *in;
    src.os = in->src_os ? in->src_os : 1;
    if (in->reg_only_push) {
        if (chris_read_gpr(cpu, in, 0, src.os, &v) != 0) {
            return -1;
        }
    } else if (chris_read_rm(cpu, &src, &v) != 0) {
        return -1;
    }
    if (sign) {
        v = sx_to(v, src.os);
    }
    if (in->reg_only_push) {
        return chris_write_gpr(cpu, in, 0, in->os, v);
    }
    return chris_write_regop(cpu, in, v);
}

static int do_lea(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t ea = 0;
    if (in->mod == 3) {
        return fail_ud(cpu);
    }
    if (chris_eff_addr(cpu, in, &ea) != 0) {
        return -1;
    }
    return chris_write_regop(cpu, in, ea);
}

static int do_xchg(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t a = 0;
    uint64_t b = 0;
    if (chris_read_regop(cpu, in, &a) != 0 || chris_read_rm(cpu, in, &b) != 0) {
        return -1;
    }
    if (chris_write_regop(cpu, in, b) != 0) {
        return -1;
    }
    return chris_write_rm(cpu, in, a);
}

static int do_push_val(ChrisCpu *cpu, const ChrisInsn *in, uint64_t v) {
    if (in->os == 2) {
        uint64_t rsp = cpu->arch.rsp - 2ull;
        uint16_t w = (uint16_t)v;
        if (chris_va_write(cpu, rsp, &w, 2) != 0) {
            return -1;
        }
        cpu->arch.rsp = rsp;
        return 0;
    }
    return chris_push8(cpu, v);
}

static int do_push(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    if (in->imm_src) {
        v = chris_imm_sx(in);
    } else if (in->reg_only_push) {
        if (chris_read_gpr(cpu, in, in->rm, in->os == 2 ? 2 : 8, &v) != 0) {
            return -1;
        }
    } else if (chris_read_rm(cpu, in, &v) != 0) {
        return -1;
    }
    return do_push_val(cpu, in, v);
}

static int do_pop(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    if (in->os == 2) {
        uint16_t w = 0;
        if (chris_va_read(cpu, cpu->arch.rsp, &w, 2, 0) != 0) {
            return -1;
        }
        cpu->arch.rsp += 2ull;
        v = w;
    } else if (chris_pop8(cpu, &v) != 0) {
        return -1;
    }
    if (in->reg_only_push) {
        return chris_write_gpr(cpu, in, in->rm, in->os == 2 ? 2 : 8, v);
    }
    return chris_write_rm(cpu, in, v);
}

static int do_jcc(ChrisCpu *cpu, const ChrisInsn *in) {
    int64_t rel = (int64_t)chris_imm_sx(in);
    if (chris_cc_true(in->cc, cpu->arch.rflags)) {
        cpu->arch.rip = cpu->arch.rip + (uint64_t)in->len + (uint64_t)rel;
        cpu->rip_dirty = 1;
    }
    return 0;
}

static int do_jmp(ChrisCpu *cpu, const ChrisInsn *in) {
    if (in->has_modrm && in->imm_bytes == 0) {
        uint64_t v = 0;
        if (chris_read_rm(cpu, in, &v) != 0) {
            return -1;
        }
        cpu->arch.rip = v;
    } else {
        cpu->arch.rip = cpu->arch.rip + (uint64_t)in->len + (uint64_t)chris_imm_sx(in);
    }
    cpu->rip_dirty = 1;
    return 0;
}

static int do_call(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t next = cpu->arch.rip + (uint64_t)in->len;
    uint64_t dest;
    if (in->has_modrm && in->imm_bytes == 0) {
        if (chris_read_rm(cpu, in, &dest) != 0) {
            return -1;
        }
    } else {
        dest = next + (uint64_t)chris_imm_sx(in);
    }
    if (chris_push8(cpu, next) != 0) {
        return -1;
    }
    cpu->arch.rip = dest;
    cpu->rip_dirty = 1;
    return 0;
}

static int do_ret(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    if (chris_pop8(cpu, &v) != 0) {
        return -1;
    }
    cpu->arch.rsp += in->imm;
    cpu->arch.rip = v;
    cpu->rip_dirty = 1;
    return 0;
}

static int do_shift(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    uint64_t count;
    int bits = in->os * 8;
    int mask = in->os == 8 ? 63 : 31;
    uint64_t sb = 1ull << (bits - 1);
    int i;
    int cf = (cpu->arch.rflags & 1ull) != 0;
    int of = 0;
    if (chris_read_rm(cpu, in, &v) != 0) {
        return -1;
    }
    if (in->shift_cl) {
        count = cpu->arch.gpr[CHRIS_GPR_RCX] & 0xffull;
    } else if (in->shift_imm) {
        count = in->imm & 0xffull;
    } else {
        count = 1;
    }
    count &= (uint64_t)mask;
    if (count == 0) {
        return 0;
    }
    v &= (bits == 64) ? ~0ull : ((1ull << bits) - 1ull);
    for (i = 0; i < (int)count; ++i) {
        if (in->shift_kind == CHRIS_SH_SHL || in->shift_kind == CHRIS_SH_ROL) {
            cf = (v & sb) != 0;
            v = (v << 1) & ((bits == 64) ? ~0ull : ((1ull << bits) - 1ull));
            if (in->shift_kind == CHRIS_SH_ROL && cf) {
                v |= 1ull;
            }
        } else if (in->shift_kind == CHRIS_SH_SHR || in->shift_kind == CHRIS_SH_ROR) {
            cf = (v & 1ull) != 0;
            v >>= 1;
            if (in->shift_kind == CHRIS_SH_ROR && cf) {
                v |= sb;
            }
        } else {
            int sign = (v & sb) != 0;
            cf = (v & 1ull) != 0;
            v >>= 1;
            if (sign) {
                v |= sb;
            }
        }
    }
    if (count == 1) {
        if (in->shift_kind == CHRIS_SH_SHL || in->shift_kind == CHRIS_SH_ROL) {
            of = ((v & sb) != 0) != cf;
        } else if (in->shift_kind == CHRIS_SH_SHR) {
            of = (v & sb) != 0;
        } else {
            of = 0;
        }
    }
    cpu->arch.rflags = chris_flags_bin(CHRIS_ALU_AND, v, ~0ull, in->os, cpu->arch.rflags, 0);
    cpu->arch.rflags &= ~((1ull << 0) | (1ull << 11));
    if (cf) {
        cpu->arch.rflags |= 1ull;
    }
    if (count == 1 && of) {
        cpu->arch.rflags |= 1ull << 11;
    }
    return chris_write_rm(cpu, in, v);
}

static int do_unary(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t v = 0;
    uint64_t r;
    if (chris_read_rm(cpu, in, &v) != 0) {
        return -1;
    }
    if (in->unary == CHRIS_UN_NOT) {
        r = ~v;
        return chris_write_rm(cpu, in, r);
    }
    if (in->unary == CHRIS_UN_NEG) {
        cpu->arch.rflags = chris_flags_bin(CHRIS_ALU_SUB, 0, v, in->os, cpu->arch.rflags, &r);
        return chris_write_rm(cpu, in, r);
    }
    if (in->unary == CHRIS_UN_INC) {
        uint64_t old_cf = cpu->arch.rflags & 1ull;
        cpu->arch.rflags = chris_flags_bin(CHRIS_ALU_ADD, v, 1, in->os, cpu->arch.rflags, &r);
        cpu->arch.rflags = (cpu->arch.rflags & ~1ull) | old_cf;
        return chris_write_rm(cpu, in, r);
    }
    {
        uint64_t old_cf = cpu->arch.rflags & 1ull;
        cpu->arch.rflags = chris_flags_bin(CHRIS_ALU_SUB, v, 1, in->os, cpu->arch.rflags, &r);
        cpu->arch.rflags = (cpu->arch.rflags & ~1ull) | old_cf;
        return chris_write_rm(cpu, in, r);
    }
}

static void mul_flags(ChrisCpu *cpu, int wide) {
    cpu->arch.rflags &= ~((1ull << 0) | (1ull << 11));
    if (wide) {
        cpu->arch.rflags |= (1ull << 0) | (1ull << 11);
    }
}

static int quot_fits_unsigned(unsigned __int128 q, int bits) {
    if (bits >= 64) {
        return q <= ~(unsigned __int128)0 >> 64;
    }
    return q < ((unsigned __int128)1 << bits);
}

static int quot_fits_signed(__int128 q, int bits) {
    __int128 limit;
    if (bits >= 64) {
        return q <= (__int128)INT64_MAX && q >= (__int128)INT64_MIN;
    }
    limit = (__int128)1 << (bits - 1);
    return q < limit && q >= -limit;
}

static void store_div_result(ChrisCpu *cpu, int os, uint64_t quot, uint64_t rem) {
    if (os == 1) {
        cpu->arch.rax = (cpu->arch.rax & ~0xffffull) | ((rem & 0xffull) << 8) | (quot & 0xffull);
        return;
    }
    if (os == 2) {
        cpu->arch.rax = (cpu->arch.rax & ~0xffffull) | (quot & 0xffffull);
        cpu->arch.rdx = (cpu->arch.rdx & ~0xffffull) | (rem & 0xffffull);
        return;
    }
    if (os == 4) {
        cpu->arch.rax = quot & 0xffffffffull;
        cpu->arch.rdx = rem & 0xffffffffull;
        return;
    }
    cpu->arch.rax = quot;
    cpu->arch.rdx = rem;
}

static int do_muldiv(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t src = 0;
    int os = in->os;
    uint64_t mask = os == 1 ? 0xffull : os == 2 ? 0xffffull : os == 4 ? 0xffffffffull : ~0ull;
    if (in->muldiv == CHRIS_MD_IMUL2) {
        uint64_t a = 0;
        unsigned __int128 prod;
        if (chris_read_regop(cpu, in, &a) != 0 || chris_read_rm(cpu, in, &src) != 0) {
            return -1;
        }
        prod = (unsigned __int128)(int64_t)sx_to(a, os) * (unsigned __int128)(int64_t)sx_to(src, os);
        mul_flags(cpu, (uint64_t)prod != (uint64_t)(int64_t)sx_to((uint64_t)prod, os));
        return chris_write_regop(cpu, in, (uint64_t)prod);
    }
    if (chris_read_rm(cpu, in, &src) != 0) {
        return -1;
    }
    src &= mask;
    if (in->muldiv == CHRIS_MD_MUL || in->muldiv == CHRIS_MD_IMUL) {
        unsigned __int128 prod;
        uint64_t rax = cpu->arch.gpr[CHRIS_GPR_RAX] & mask;
        int wide;
        if (in->muldiv == CHRIS_MD_IMUL) {
            prod = (unsigned __int128)(int64_t)sx_to(rax, os) * (unsigned __int128)(int64_t)sx_to(src, os);
            wide = (uint64_t)prod != (uint64_t)(int64_t)sx_to((uint64_t)prod, os);
        } else {
            prod = (unsigned __int128)rax * src;
            wide = os == 8 ? (prod >> 64) != 0 : ((uint64_t)(prod >> (os * 8))) != 0;
        }
        if (os == 8) {
            cpu->arch.rax = (uint64_t)prod;
            cpu->arch.rdx = (uint64_t)(prod >> 64);
        } else if (os == 1) {
            cpu->arch.rax = (cpu->arch.rax & ~0xffffull) | ((uint64_t)prod & 0xffffull);
        } else if (os == 2) {
            cpu->arch.rax = (cpu->arch.rax & ~0xffffull) | ((uint64_t)prod & 0xffffull);
            cpu->arch.rdx = (cpu->arch.rdx & ~0xffffull) | (((uint64_t)prod >> 16) & 0xffffull);
        } else {
            cpu->arch.rax = (uint64_t)prod & 0xffffffffull;
            cpu->arch.rdx = ((uint64_t)prod >> 32) & 0xffffffffull;
        }
        mul_flags(cpu, wide);
        return 0;
    }
    {
        int idiv = in->muldiv == CHRIS_MD_IDIV;
        unsigned __int128 udividend;
        __int128 sdividend;
        __int128 divs;
        __int128 sq;
        unsigned __int128 uq;
        uint64_t quot;
        uint64_t rem;
        if (src == 0) {
            return chris_raise(cpu, CHRIS_EX_DE, 0, 0);
        }
        if (os == 1) {
            udividend = cpu->arch.rax & 0xffffull;
            sdividend = (int16_t)udividend;
        } else if (os == 2) {
            udividend = ((cpu->arch.rdx & 0xffffull) << 16) | (cpu->arch.rax & 0xffffull);
            sdividend = (int32_t)udividend;
        } else if (os == 4) {
            udividend = (unsigned __int128)(cpu->arch.rax & 0xffffffffull) |
                        ((unsigned __int128)(cpu->arch.rdx & 0xffffffffull) << 32);
            sdividend = (int64_t)(uint64_t)udividend;
        } else {
            udividend = ((unsigned __int128)cpu->arch.rdx << 64) | cpu->arch.rax;
            sdividend = ((__int128)(int64_t)cpu->arch.rdx << 64) | (unsigned __int128)cpu->arch.rax;
        }
        if (idiv) {
            divs = (int64_t)sx_to(src, os);
            sq = sdividend / divs;
            if (!quot_fits_signed(sq, os * 8)) {
                return chris_raise(cpu, CHRIS_EX_DE, 0, 0);
            }
            quot = (uint64_t)(int64_t)sq;
            rem = (uint64_t)(int64_t)(sdividend % divs);
        } else {
            uq = udividend / src;
            if (!quot_fits_unsigned(uq, os * 8)) {
                return chris_raise(cpu, CHRIS_EX_DE, 0, 0);
            }
            quot = (uint64_t)uq;
            rem = (uint64_t)(udividend % src);
        }
        store_div_result(cpu, os, quot, rem);
    }
    return 0;
}

static int do_io(ChrisCpu *cpu, const ChrisInsn *in, int out) {
    uint32_t port;
    uint64_t wide = 0;
    uint32_t value = 0;
    int size = in->os == 1 ? 1 : in->os == 2 ? 2 : 4;
    if (in->imm_src) {
        port = (uint32_t)(in->imm & 0xffu);
    } else {
        port = (uint32_t)(cpu->arch.gpr[CHRIS_GPR_RDX] & 0xffffu);
    }
    if (out) {
        if (chris_read_gpr(cpu, in, CHRIS_GPR_RAX, size, &wide) != 0) {
            return -1;
        }
        if (cpu->machine->cfg.trace_io) {
            chris_log(cpu->machine, "io out");
        }
        return chris_io_out(cpu->machine, (uint16_t)port, size, (uint32_t)wide);
    }
    if (chris_io_in(cpu->machine, (uint16_t)port, size, &value) != 0) {
        return -1;
    }
    return chris_write_gpr(cpu, in, CHRIS_GPR_RAX, size, value);
}

static uint64_t *cr_ptr(ChrisCpu *cpu, int n) {
    if (n == 0) {
        return &cpu->arch.cr0;
    }
    if (n == 2) {
        return &cpu->arch.cr2;
    }
    if (n == 3) {
        return &cpu->arch.cr3;
    }
    if (n == 4) {
        return &cpu->arch.cr4;
    }
    if (n == 8) {
        return &cpu->arch.cr8;
    }
    return 0;
}

static int msr_access(ChrisCpu *cpu, int write) {
    uint32_t index = (uint32_t)cpu->arch.gpr[CHRIS_GPR_RCX];
    uint64_t *slot = 0;
    if (index == CHRIS_EFER) {
        slot = &cpu->arch.efer;
    } else if (index == CHRIS_MSR_STAR) {
        slot = &cpu->arch.star;
    } else if (index == CHRIS_MSR_LSTAR) {
        slot = &cpu->arch.lstar;
    } else if (index == CHRIS_MSR_CSTAR) {
        slot = &cpu->arch.cstar;
    } else if (index == CHRIS_MSR_FMASK) {
        slot = &cpu->arch.fmask;
    } else if (index == CHRIS_MSR_FS_BASE) {
        slot = &cpu->arch.fs_base;
    } else if (index == CHRIS_MSR_GS_BASE) {
        slot = &cpu->arch.gs_base;
    } else if (index == CHRIS_MSR_KERNEL_GS) {
        slot = &cpu->arch.kernel_gs_base;
    } else if (index == CHRIS_MSR_APIC_BASE) {
        slot = &cpu->arch.apic_base;
    } else {
        return chris_raise(cpu, CHRIS_EX_GP, 1, 0);
    }
    if (write) {
        *slot = (cpu->arch.gpr[CHRIS_GPR_RDX] << 32) | (cpu->arch.gpr[CHRIS_GPR_RAX] & 0xffffffffull);
        return 0;
    }
    cpu->arch.gpr[CHRIS_GPR_RAX] = *slot & 0xffffffffull;
    cpu->arch.gpr[CHRIS_GPR_RDX] = *slot >> 32;
    return 0;
}

static int do_desc(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t ea = 0;
    uint8_t buf[10];
    if (chris_eff_addr(cpu, in, &ea) != 0) {
        return -1;
    }
    if (in->desc_op == CHRIS_DESC_LGDT || in->desc_op == CHRIS_DESC_LIDT) {
        uint16_t limit;
        uint64_t base;
        if (chris_va_read(cpu, ea, buf, 10, 0) != 0) {
            return -1;
        }
        memcpy(&limit, buf, 2);
        memcpy(&base, buf + 2, 8);
        if (in->desc_op == CHRIS_DESC_LGDT) {
            cpu->arch.gdtr.limit = limit;
            cpu->arch.gdtr.base = base;
        } else {
            cpu->arch.idtr.limit = limit;
            cpu->arch.idtr.base = base;
        }
        return 0;
    }
    memset(buf, 0, sizeof buf);
    if (in->desc_op == CHRIS_DESC_SGDT) {
        memcpy(buf, &cpu->arch.gdtr.limit, 2);
        memcpy(buf + 2, &cpu->arch.gdtr.base, 8);
    } else {
        memcpy(buf, &cpu->arch.idtr.limit, 2);
        memcpy(buf + 2, &cpu->arch.idtr.base, 8);
    }
    return chris_va_write(cpu, ea, buf, 10);
}

static void string_dec_rcx(ChrisCpu *cpu, const ChrisInsn *in, uint64_t n) {
    cpu->arch.rcx -= n;
    if (in->asz == 4) {
        cpu->arch.rcx &= 0xffffffffull;
    }
}

static int write_pattern(ChrisCpu *cpu, uint64_t pa, uint64_t value, int os, uint64_t count) {
    uint8_t buf[4096];
    size_t bytes = (size_t)count * (size_t)os;
    size_t i;
    if (bytes == 0 || bytes > sizeof buf) {
        return -1;
    }
    for (i = 0; i < bytes; i += (size_t)os) {
        memcpy(buf + i, &value, (size_t)os);
    }
    return chris_phys_write(cpu->machine, pa, buf, bytes);
}

static int do_stos(ChrisCpu *cpu, const ChrisInsn *in) {
    uint64_t left;
    uint64_t value = cpu->arch.rax;
    int os = in->os;
    int df = (cpu->arch.rflags & (1ull << 10)) != 0;
    if (os != 1 && os != 2 && os != 4 && os != 8) {
        return fail_ud(cpu);
    }
    if (in->rep) {
        left = in->asz == 4 ? (cpu->arch.rcx & 0xffffffffull) : cpu->arch.rcx;
    } else {
        left = 1;
    }
    while (left != 0) {
        uint64_t chunk = 1;
        if (!df) {
            uint64_t pa = 0;
            uint32_t err = 0;
            uint64_t room;
            int tr = chris_translate(cpu, cpu->arch.rdi, &pa, 1, &err);
            if (tr == 0) {
                room = 0x1000ull - (pa & 0xfffull);
                chunk = left;
                if (chunk * (uint64_t)os > room) {
                    chunk = room / (uint64_t)os;
                }
                if (chunk == 0 || write_pattern(cpu, pa, value, os, chunk) != 0) {
                    chunk = 1;
                    if (chris_va_write(cpu, cpu->arch.rdi, &value, (size_t)os) != 0) {
                        return -1;
                    }
                }
            } else if (chris_va_write(cpu, cpu->arch.rdi, &value, (size_t)os) != 0) {
                return -1;
            }
        } else if (chris_va_write(cpu, cpu->arch.rdi, &value, (size_t)os) != 0) {
            return -1;
        }
        if (df) {
            cpu->arch.rdi -= (uint64_t)os;
        } else {
            cpu->arch.rdi += chunk * (uint64_t)os;
        }
        if (in->asz == 4) {
            cpu->arch.rdi &= 0xffffffffull;
        }
        left -= chunk;
        if (in->rep) {
            string_dec_rcx(cpu, in, chunk);
        }
        if (in->rep && cpu->irq_pending && (cpu->arch.rflags & (1ull << 9)) != 0 && !cpu->sti_delay &&
            left != 0) {
            cpu->rip_dirty = 1;
            return 0;
        }
    }
    return 0;
}

int chris_execute(ChrisCpu *cpu, const ChrisInsn *in) {
    if (!cpu || !in) {
        return -1;
    }
    if (cpu->halted) {
        return -1;
    }
    switch (in->op) {
    case CHRIS_OP_ALU:
        return do_alu(cpu, in);
    case CHRIS_OP_MOV:
        return do_mov(cpu, in);
    case CHRIS_OP_MOVZX:
        return do_extend(cpu, in, 0);
    case CHRIS_OP_MOVSX:
        return do_extend(cpu, in, 1);
    case CHRIS_OP_LEA:
        return do_lea(cpu, in);
    case CHRIS_OP_XCHG:
        return do_xchg(cpu, in);
    case CHRIS_OP_PUSH:
        return do_push(cpu, in);
    case CHRIS_OP_POP:
        return do_pop(cpu, in);
    case CHRIS_OP_PUSHF:
        return do_push_val(cpu, in, cpu->arch.rflags & 0x00000000fcffffull);
    case CHRIS_OP_POPF: {
        uint64_t v = 0;
        if (in->os == 8) {
            if (chris_pop8(cpu, &v) != 0) {
                return -1;
            }
        } else {
            return fail_ud(cpu);
        }
        cpu->arch.rflags = (v & 0x00000000fcffffull) | 2ull;
        return 0;
    }
    case CHRIS_OP_JMP:
        return do_jmp(cpu, in);
    case CHRIS_OP_JCC:
        return do_jcc(cpu, in);
    case CHRIS_OP_CALL:
        return do_call(cpu, in);
    case CHRIS_OP_RET:
        return do_ret(cpu, in);
    case CHRIS_OP_SHIFT:
        return do_shift(cpu, in);
    case CHRIS_OP_UNARY:
        return do_unary(cpu, in);
    case CHRIS_OP_MULDIV:
        return do_muldiv(cpu, in);
    case CHRIS_OP_IN:
        return do_io(cpu, in, 0);
    case CHRIS_OP_OUT:
        return do_io(cpu, in, 1);
    case CHRIS_OP_INT: {
        uint64_t next = cpu->arch.rip + (uint64_t)in->len;
        cpu->arch.rip = next;
        cpu->rip_dirty = 1;
        return chris_raise(cpu, in->vector, 0, 0);
    }
    case CHRIS_OP_IRETQ: {
        uint64_t rip, cs, rf, rsp, ss;
        if (chris_pop8(cpu, &rip) != 0 || chris_pop8(cpu, &cs) != 0 || chris_pop8(cpu, &rf) != 0 ||
            chris_pop8(cpu, &rsp) != 0 || chris_pop8(cpu, &ss) != 0) {
            return -1;
        }
        cpu->arch.rip = rip;
        cpu->arch.rsp = rsp;
        cpu->arch.rflags = (rf & 0x00000000fcffffull) | 2ull;
        cpu->arch.ss.sel = (uint16_t)ss;
        cpu->arch.cs.sel = (uint16_t)cs;
        cpu->rip_dirty = 1;
        return 0;
    }
    case CHRIS_OP_STOS:
        return do_stos(cpu, in);
    case CHRIS_OP_HLT:
        cpu->halted = 1;
        cpu->exit_reason = CHRIS_EXIT_HLT;
        cpu->rip_dirty = 1;
        cpu->arch.rip += (uint64_t)in->len;
        return 0;
    case CHRIS_OP_NOP:
        return 0;
    case CHRIS_OP_FLAG:
        if (in->flag_op == CHRIS_FLAG_CLC) {
            cpu->arch.rflags &= ~1ull;
        } else if (in->flag_op == CHRIS_FLAG_STC) {
            cpu->arch.rflags |= 1ull;
        } else if (in->flag_op == CHRIS_FLAG_CLD) {
            cpu->arch.rflags &= ~(1ull << 10);
        } else if (in->flag_op == CHRIS_FLAG_STD) {
            cpu->arch.rflags |= 1ull << 10;
        } else if (in->flag_op == CHRIS_FLAG_CLI) {
            cpu->arch.rflags &= ~(1ull << 9);
        } else if (in->flag_op == CHRIS_FLAG_STI) {
            cpu->arch.rflags |= 1ull << 9;
            cpu->sti_delay = 1;
        }
        return 0;
    case CHRIS_OP_LEAVE: {
        uint64_t v = 0;
        cpu->arch.rsp = cpu->arch.gpr[CHRIS_GPR_RBP];
        if (chris_pop8(cpu, &v) != 0) {
            return -1;
        }
        cpu->arch.gpr[CHRIS_GPR_RBP] = v;
        return 0;
    }
    case CHRIS_OP_CPUID: {
        uint32_t a, b, c, d;
        chris_cpuid((uint32_t)cpu->arch.gpr[CHRIS_GPR_RAX], (uint32_t)cpu->arch.gpr[CHRIS_GPR_RCX], &a, &b,
                    &c, &d);
        cpu->arch.gpr[CHRIS_GPR_RAX] = a;
        cpu->arch.gpr[CHRIS_GPR_RBX] = b;
        cpu->arch.gpr[CHRIS_GPR_RCX] = c;
        cpu->arch.gpr[CHRIS_GPR_RDX] = d;
        return 0;
    }
    case CHRIS_OP_RDMSR:
        return msr_access(cpu, 0);
    case CHRIS_OP_WRMSR:
        return msr_access(cpu, 1);
    case CHRIS_OP_MOVCR: {
        uint64_t *cr = cr_ptr(cpu, in->reg);
        uint64_t v = 0;
        if (!cr) {
            return fail_ud(cpu);
        }
        if (in->cr_to_reg) {
            return chris_write_gpr(cpu, in, in->rm, 8, *cr);
        }
        if (chris_read_gpr(cpu, in, in->rm, 8, &v) != 0) {
            return -1;
        }
        *cr = v;
        if (in->digit == 3) {
            cpu->tlb_gen++;
        }
        return 0;
    }
    case CHRIS_OP_DESC:
        return do_desc(cpu, in);
    case CHRIS_OP_SETCC: {
        uint64_t v = chris_cc_true(in->cc, cpu->arch.rflags) ? 1ull : 0ull;
        return chris_write_rm(cpu, in, v);
    }
    case CHRIS_OP_CMOV: {
        uint64_t v = 0;
        if (!chris_cc_true(in->cc, cpu->arch.rflags)) {
            return 0;
        }
        if (chris_read_rm(cpu, in, &v) != 0) {
            return -1;
        }
        return chris_write_regop(cpu, in, v);
    }
    case CHRIS_OP_UNIMPL:
    case CHRIS_OP_UD:
    default:
        return fail_ud(cpu);
    }
}
