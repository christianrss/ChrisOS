#include "machine/machine.h"

static int canonical(uint64_t va) {
    uint64_t top = va >> 47;
    return top == 0 || top == 0x1ffffull;
}

static int read_pte(ChrisCpu *cpu, uint64_t pa, uint64_t *entry) {
    return chris_phys_read(cpu->machine, pa, entry, 8);
}

static int write_pte(ChrisCpu *cpu, uint64_t pa, uint64_t entry) {
    return chris_phys_write(cpu->machine, pa, &entry, 8);
}

int chris_translate(ChrisCpu *cpu, uint64_t va, uint64_t *pa, int access, uint32_t *err) {
    uint64_t table;
    uint64_t entry;
    int level;
    int user = cpu->arch.cpl == 3;
    int write = access == 1;
    int exec = access == 2;
    static const int shift[4] = {39, 30, 21, 12};
    uint32_t fault = 0;

    if (err) {
        *err = 0;
    }
    if ((cpu->arch.cr0 & CHRIS_CR0_PG) == 0) {
        *pa = va;
        return 0;
    }
    if (!canonical(va)) {
        if (err) {
            *err = 0;
        }
        return -2;
    }
    table = cpu->arch.cr3 & ~0xfffull;
    for (level = 0; level < 4; ++level) {
        uint64_t index = (va >> shift[level]) & 0x1ffull;
        uint64_t addr = table + index * 8ull;
        if (read_pte(cpu, addr, &entry) != 0) {
            return -3;
        }
        if ((entry & 1ull) == 0) {
            fault = (write ? 2u : 0u) | (user ? 4u : 0u) | (exec ? 16u : 0u);
            if (err) {
                *err = fault;
            }
            return -1;
        }
        if (write && (entry & 2ull) == 0) {
            if (user || (cpu->arch.cr0 & CHRIS_CR0_WP) != 0) {
                fault = 1u | 2u | (user ? 4u : 0u);
                if (err) {
                    *err = fault;
                }
                return -1;
            }
        }
        if (user && (entry & 4ull) == 0) {
            fault = 1u | (write ? 2u : 0u) | 4u | (exec ? 16u : 0u);
            if (err) {
                *err = fault;
            }
            return -1;
        }
        if (exec && (cpu->arch.efer & CHRIS_EFER_NXE) != 0 && (entry & (1ull << 63)) != 0) {
            fault = 1u | 16u | (user ? 4u : 0u);
            if (err) {
                *err = fault;
            }
            return -1;
        }
        if ((entry & 1ull) != 0 && (entry & (1ull << 5)) == 0) {
            entry |= 1ull << 5;
            (void)write_pte(cpu, addr, entry);
        }
        if ((entry & (1ull << 7)) != 0 && (level == 1 || level == 2)) {
            uint64_t page;
            uint64_t mask;
            if (level == 1) {
                mask = (1ull << 30) - 1ull;
                page = entry & 0x000fffffc0000000ull;
            } else {
                mask = (1ull << 21) - 1ull;
                page = entry & 0x000fffffffe00000ull;
            }
            if (write && (entry & (1ull << 6)) == 0) {
                entry |= 1ull << 6;
                (void)write_pte(cpu, addr, entry);
            }
            *pa = page | (va & mask);
            return 0;
        }
        if (level == 3) {
            if (write && (entry & (1ull << 6)) == 0) {
                entry |= 1ull << 6;
                (void)write_pte(cpu, addr, entry);
            }
            *pa = (entry & ~0xfffull) | (va & 0xfffull);
            return 0;
        }
        table = entry & ~0xfffull;
    }
    return -1;
}

int chris_va_read(ChrisCpu *cpu, uint64_t va, void *dst, size_t n, int access) {
    uint8_t *out = (uint8_t *)dst;
    size_t done = 0;
    if (n == 0) {
        return 0;
    }
    while (done < n) {
        uint64_t pa = 0;
        uint32_t err = 0;
        uint64_t cur = va + done;
        size_t chunk;
        int tr = chris_translate(cpu, cur, &pa, access, &err);
        if (tr != 0) {
            if (cpu->delivering) {
                return -1;
            }
            if (tr == -2) {
                chris_raise(cpu, CHRIS_EX_GP, 1, 0);
            } else if (tr == -3) {
                cpu->exit_reason = CHRIS_EXIT_UNMAPPED;
                cpu->halted = 1;
                cpu->arch.cr2 = cur;
            } else {
                cpu->arch.cr2 = cur;
                chris_raise(cpu, CHRIS_EX_PF, 1, err);
            }
            return -1;
        }
        chunk = 0x1000u - (size_t)(pa & 0xfffu);
        if (chunk > n - done) {
            chunk = n - done;
        }
        if (chris_phys_read(cpu->machine, pa, out + done, chunk) != 0) {
            if (!cpu->delivering) {
                cpu->exit_reason = CHRIS_EXIT_UNMAPPED;
                cpu->halted = 1;
                cpu->arch.cr2 = cur;
            }
            return -1;
        }
        done += chunk;
    }
    return 0;
}

int chris_va_write(ChrisCpu *cpu, uint64_t va, const void *src, size_t n) {
    const uint8_t *in = (const uint8_t *)src;
    size_t done = 0;
    if (n == 0) {
        return 0;
    }
    while (done < n) {
        uint64_t pa = 0;
        uint32_t err = 0;
        uint64_t cur = va + done;
        size_t chunk;
        int tr = chris_translate(cpu, cur, &pa, 1, &err);
        if (tr != 0) {
            if (cpu->delivering) {
                return -1;
            }
            if (tr == -2) {
                chris_raise(cpu, CHRIS_EX_GP, 1, 0);
            } else if (tr == -3) {
                cpu->exit_reason = CHRIS_EXIT_UNMAPPED;
                cpu->halted = 1;
                cpu->arch.cr2 = cur;
            } else {
                cpu->arch.cr2 = cur;
                chris_raise(cpu, CHRIS_EX_PF, 1, err);
            }
            return -1;
        }
        chunk = 0x1000u - (size_t)(pa & 0xfffu);
        if (chunk > n - done) {
            chunk = n - done;
        }
        if (chris_phys_write(cpu->machine, pa, in + done, chunk) != 0) {
            if (!cpu->delivering) {
                cpu->exit_reason = CHRIS_EXIT_UNMAPPED;
                cpu->halted = 1;
                cpu->arch.cr2 = cur;
            }
            return -1;
        }
        done += chunk;
    }
    return 0;
}
