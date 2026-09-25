#include "machine/machine.h"

#include <stdio.h>
#include <string.h>

typedef struct Elf64Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64Ehdr;

typedef struct Elf64Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64Phdr;

static void set_err(char *err, size_t cap, const char *msg) {
    if (err && cap) {
        snprintf(err, cap, "%s", msg);
    }
}

static int overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    if (an == 0 || bn == 0) {
        return 0;
    }
    return a < b + bn && b < a + an;
}

int chris_load_elf(ChrisMachine *m, const uint8_t *image, size_t n, char *err, size_t errcap) {
    Elf64Ehdr eh;
    uint16_t pi;
    if (!m || !image) {
        set_err(err, errcap, "missing image");
        return -1;
    }
    if (n < sizeof eh) {
        set_err(err, errcap, "truncated ELF");
        return -1;
    }
    memcpy(&eh, image, sizeof eh);
    if (eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F') {
        set_err(err, errcap, "bad ELF magic");
        return -1;
    }
    if (eh.e_ident[4] != 2 || eh.e_ident[5] != 1) {
        set_err(err, errcap, "ELF is not 64-bit little-endian");
        return -1;
    }
    if (eh.e_machine != 62) {
        set_err(err, errcap, "ELF machine is not x86-64");
        return -1;
    }
    if (eh.e_phentsize != sizeof(Elf64Phdr) || eh.e_phnum == 0) {
        set_err(err, errcap, "ELF program headers missing");
        return -1;
    }
    if (eh.e_phoff > n || (size_t)eh.e_phnum * sizeof(Elf64Phdr) > n - eh.e_phoff) {
        set_err(err, errcap, "ELF program headers out of range");
        return -1;
    }
    for (pi = 0; pi < eh.e_phnum; ++pi) {
        Elf64Phdr ph;
        memcpy(&ph, image + eh.e_phoff + (size_t)pi * sizeof ph, sizeof ph);
        if (ph.p_type != 1u) {
            continue;
        }
        if (ph.p_vaddr >= 0xffff800000000000ull) {
            set_err(err, errcap, "higher-half ELF is outside boot protocol v1");
            return -1;
        }
        if (ph.p_memsz < ph.p_filesz || ph.p_offset > n || ph.p_filesz > n - ph.p_offset) {
            set_err(err, errcap, "ELF segment out of range");
            return -1;
        }
        if (ph.p_vaddr >= m->ram_size || ph.p_memsz > m->ram_size - ph.p_vaddr) {
            set_err(err, errcap, "ELF segment does not fit in guest RAM");
            return -1;
        }
        if (overlap(ph.p_vaddr, ph.p_memsz, m->ram_size - CHRIS_PT_RESERVE, CHRIS_PT_RESERVE) ||
            overlap(ph.p_vaddr, ph.p_memsz, CHRIS_GDT_PHYS, 0x1000ull) ||
            overlap(ph.p_vaddr, ph.p_memsz, CHRIS_STACK_RSP - 0x1000ull, 0x1000ull)) {
            set_err(err, errcap, "ELF segment overlaps boot protocol memory");
            return -1;
        }
        memcpy(m->ram + ph.p_vaddr, image + ph.p_offset, (size_t)ph.p_filesz);
        if (ph.p_memsz > ph.p_filesz) {
            memset(m->ram + ph.p_vaddr + ph.p_filesz, 0, (size_t)(ph.p_memsz - ph.p_filesz));
        }
    }
    if (eh.e_entry >= 0xffff800000000000ull || eh.e_entry >= m->ram_size) {
        set_err(err, errcap, "ELF entry is outside boot protocol v1 RAM");
        return -1;
    }
    m->entry = eh.e_entry;
    return 0;
}

static int install_tables(ChrisMachine *m, ChrisArchitectureState *st) {
    uint64_t pml4 = m->ram_size - 0x4000ull;
    uint64_t pdpt = m->ram_size - 0x3000ull;
    uint64_t pd = m->ram_size - 0x2000ull;
    uint64_t gdt = CHRIS_GDT_PHYS;
    uint64_t i;
    uint64_t pages;
    uint64_t desc[3];
    memset(m->ram + pml4, 0, 0x3000);
    *(uint64_t *)(m->ram + pml4) = pdpt | 0x3ull;
    *(uint64_t *)(m->ram + pdpt) = pd | 0x3ull;
    pages = m->ram_size / (2ull * 1024ull * 1024ull);
    if (pages > 512ull) {
        return -1;
    }
    for (i = 0; i < pages; ++i) {
        *(uint64_t *)(m->ram + pd + i * 8ull) = (i * 2ull * 1024ull * 1024ull) | 0x83ull;
    }
    desc[0] = 0;
    desc[1] = 0x00af9a000000ffffull;
    desc[2] = 0x00cf92000000ffffull;
    memcpy(m->ram + gdt, desc, sizeof desc);
    st->cr3 = pml4;
    st->cr0 = CHRIS_CR0_PE | CHRIS_CR0_NE | CHRIS_CR0_WP | CHRIS_CR0_PG;
    st->cr4 = CHRIS_CR4_PAE;
    st->efer = CHRIS_EFER_LME | CHRIS_EFER_LMA;
    st->gdtr.base = gdt;
    st->gdtr.limit = (uint16_t)(sizeof desc - 1u);
    st->cs.sel = 0x08;
    st->cs.base = 0;
    st->cs.limit = 0xffffffffu;
    st->cs.attr = 0xaf9a;
    st->ss.sel = 0x10;
    st->ds = st->ss;
    st->es = st->ss;
    st->rflags = 2ull;
    st->cpl = 0;
    return 0;
}

int chris_boot(ChrisMachine *m, uint64_t entry, uint64_t rsp) {
    ChrisArchitectureState st;
    if (!m || !m->cpu) {
        return -1;
    }
    if (entry == 0) {
        entry = m->entry;
    }
    if (entry == 0 || entry >= m->ram_size) {
        return -1;
    }
    if ((m->ram_size % (2ull * 1024ull * 1024ull)) != 0 || m->ram_size < 2ull * 1024ull * 1024ull) {
        return -1;
    }
    chris_arch_reset(&st);
    if (install_tables(m, &st) != 0) {
        return -1;
    }
    st.rip = entry;
    st.gpr[CHRIS_GPR_RSP] = rsp ? rsp : CHRIS_STACK_RSP;
    m->backend->set_state(m->cpu, &st);
    m->booted = 1;
    m->entry = entry;
    return 0;
}
