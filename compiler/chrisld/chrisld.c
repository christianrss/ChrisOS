/* LEARN:SH16-14 */
#include "chrisld.h"
#include <string.h>

#define ELF_MAGIC_0 0x7fu
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELF_CLASS_64 2u
#define ELF_DATA_LSB 1u
#define ELF_VERSION 1u
#define ELF_OSABI_SYSV 0u
#define ELF_TYPE_EXEC 2u
#define ELF_MACHINE_X86_64 62u
#define ELF_PHDR_SIZE 56u
#define ELF_EHDR_SIZE 64u
#define PT_LOAD 1u
#define PF_X 1u
#define PF_W 2u
#define PF_R 4u
#define LD_OBJS 32u

static void wr8(uint8_t *p, uint32_t off, uint8_t v) {
    p[off] = v;
}

static void wr16(uint8_t *p, uint32_t off, uint16_t v) {
    p[off] = (uint8_t)(v & 0xffu);
    p[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
}

static void wr32(uint8_t *p, uint32_t off, uint32_t v) {
    p[off] = (uint8_t)(v & 0xffu);
    p[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
    p[off + 2u] = (uint8_t)((v >> 16) & 0xffu);
    p[off + 3u] = (uint8_t)((v >> 24) & 0xffu);
}

static void wr64(uint8_t *p, uint32_t off, uint64_t v) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        p[off + i] = (uint8_t)((v >> (i * 8u)) & 0xffu);
    }
}

static uint32_t rd32(const uint8_t *p, uint32_t off) {
    return (uint32_t)p[off] |
           ((uint32_t)p[off + 1u] << 8) |
           ((uint32_t)p[off + 2u] << 16) |
           ((uint32_t)p[off + 3u] << 24);
}

static uint64_t rd64(const uint8_t *p, uint32_t off) {
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        v |= (uint64_t)p[off + i] << (i * 8u);
    }
    return v;
}

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

static int same_name(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int defined_sym(const ChrisoSym *s) {
    return s->binding != CHRISO_BIND_UNDEF && s->name[0] != 0;
}

static const ChrisoSym *resolve_sym(const ChrisoImage *const *imgs, uint32_t n,
                                    uint32_t obj, const ChrisoSym *sym,
                                    uint32_t *def_obj) {
    uint32_t i;
    uint32_t j;
    const ChrisoSym *found = 0;

    if (defined_sym(sym) && sym->binding != CHRISO_BIND_GLOBAL) {
        *def_obj = obj;
        return sym;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < imgs[i]->nsym; j++) {
            const ChrisoSym *s = &imgs[i]->sym[j];
            if (!defined_sym(s) || s->binding != CHRISO_BIND_GLOBAL) {
                continue;
            }
            if (!same_name(s->name, sym->name)) {
                continue;
            }
            if (found) {
                return 0;
            }
            found = s;
            *def_obj = i;
        }
    }
    if (defined_sym(sym)) {
        *def_obj = obj;
        return sym;
    }
    return found;
}

static int duplicate_globals(const ChrisoImage *const *imgs, uint32_t n) {
    uint32_t a, b, i, j;

    for (a = 0; a < n; a++) {
        for (i = 0; i < imgs[a]->nsym; i++) {
            const ChrisoSym *sa = &imgs[a]->sym[i];
            if (!defined_sym(sa) || sa->binding != CHRISO_BIND_GLOBAL) {
                continue;
            }
            for (b = a + 1u; b < n; b++) {
                for (j = 0; j < imgs[b]->nsym; j++) {
                    const ChrisoSym *sb = &imgs[b]->sym[j];
                    if (defined_sym(sb) && sb->binding == CHRISO_BIND_GLOBAL &&
                        same_name(sa->name, sb->name)) {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static uint64_t sym_addr(uint64_t load, const uint32_t *text_at,
                         uint32_t obj, const ChrisoSym *s) {
    if (s->section == CHRISO_SEC_TEXT) {
        return load + text_at[obj] + s->offset;
    }
    return load + s->offset;
}

static int apply_one(uint8_t *text, uint32_t text_len, uint32_t text_at,
                     uint64_t load, uint64_t s_addr, const ChrisoRel *rel) {
    uint32_t at = text_at + rel->offset;
    uint64_t p = load + at;
    uint64_t value = s_addr + (uint64_t)(int64_t)rel->addend;
    int32_t disp;

    if (rel->type == R_X86_64_NONE) {
        return 0;
    }
    if (rel->type == R_X86_64_64) {
        if (at + 8u > text_len) {
            return -1;
        }
        wr64(text, at, value);
        return 0;
    }
    if (rel->type == R_X86_64_PC32 || rel->type == R_X86_64_PLT32) {
        int64_t d = (int64_t)value - (int64_t)p;
        if (at + 4u > text_len || d > 2147483647ll || d < -2147483648ll) {
            return -1;
        }
        disp = (int32_t)d;
        wr32(text, at, (uint32_t)disp);
        return 0;
    }
    if (rel->type == R_X86_64_32 || rel->type == R_X86_64_32S) {
        if (at + 4u > text_len || value > 0xffffffffull) {
            return -1;
        }
        wr32(text, at, (uint32_t)value);
        return 0;
    }
    return -1;
}

int chrisld_link_objects(const ChrisoImage *const *imgs, uint32_t n,
                         uint64_t load_addr, void *out, uint32_t cap,
                         uint64_t *entry_out) {
    uint8_t *elf = out;
    uint32_t text_at[LD_OBJS];
    uint32_t text_total = 0;
    uint32_t off;
    uint32_t phoff;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t i;
    uint32_t j;
    uint64_t entry = load_addr;
    int saw_entry = 0;

    if (!imgs || !n || n > LD_OBJS || !out || !entry_out) {
        return -1;
    }
    if (duplicate_globals(imgs, n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (!imgs[i]) {
            return -1;
        }
        text_at[i] = text_total;
        if (imgs[i]->sec_size[CHRISO_SEC_TEXT] > 0) {
            if (text_total != 0u) {
                text_at[i] = align_up(text_total, 16u);
            }
            text_total = text_at[i] + imgs[i]->sec_size[CHRISO_SEC_TEXT];
        }
    }
    if (text_total == 0u) {
        return -1;
    }
    phoff = ELF_EHDR_SIZE;
    off = align_up(ELF_EHDR_SIZE + ELF_PHDR_SIZE, 16u);
    filesz = text_total;
    memsz = align_up(filesz, 4096u);
    if ((uint64_t)off + filesz > cap) {
        return -1;
    }
    memset(elf, 0, off + filesz);
    for (i = 0; i < n; i++) {
        if (imgs[i]->sec_size[CHRISO_SEC_TEXT] > 0 &&
            imgs[i]->sec[CHRISO_SEC_TEXT]) {
            memcpy(elf + off + text_at[i], imgs[i]->sec[CHRISO_SEC_TEXT],
                   imgs[i]->sec_size[CHRISO_SEC_TEXT]);
        }
        for (j = 0; j < imgs[i]->nsym; j++) {
            const ChrisoSym *s = &imgs[i]->sym[j];
            if (!defined_sym(s)) {
                continue;
            }
            if (!saw_entry && same_name(s->name, "kstart")) {
                entry = sym_addr(load_addr, text_at, i, s);
                saw_entry = 1;
            }
        }
    }
    if (!saw_entry) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < imgs[i]->nsym; j++) {
                const ChrisoSym *s = &imgs[i]->sym[j];
                if (defined_sym(s) && same_name(s->name, "main")) {
                    entry = sym_addr(load_addr, text_at, i, s);
                    saw_entry = 1;
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < imgs[i]->nrel; j++) {
            const ChrisoRel *rel = &imgs[i]->rel[j];
            const ChrisoSym *sym;
            const ChrisoSym *def;
            uint32_t def_obj = 0;
            if (rel->sym_index >= imgs[i]->nsym) {
                return -1;
            }
            sym = &imgs[i]->sym[rel->sym_index];
            def = resolve_sym(imgs, n, i, sym, &def_obj);
            if (!def) {
                return -1;
            }
            if (apply_one(elf + off, filesz, text_at[i], load_addr,
                          sym_addr(load_addr, text_at, def_obj, def),
                          rel) != 0) {
                return -1;
            }
        }
    }
    wr8(elf, 0, ELF_MAGIC_0);
    wr8(elf, 1, ELF_MAGIC_1);
    wr8(elf, 2, ELF_MAGIC_2);
    wr8(elf, 3, ELF_MAGIC_3);
    wr8(elf, 4, ELF_CLASS_64);
    wr8(elf, 5, ELF_DATA_LSB);
    wr8(elf, 6, ELF_VERSION);
    wr8(elf, 7, ELF_OSABI_SYSV);
    wr16(elf, 16, ELF_TYPE_EXEC);
    wr16(elf, 18, ELF_MACHINE_X86_64);
    wr32(elf, 20, ELF_VERSION);
    wr64(elf, 24, entry);
    wr64(elf, 32, phoff);
    wr32(elf, 48, 0);
    wr16(elf, 52, ELF_EHDR_SIZE);
    wr16(elf, 54, ELF_PHDR_SIZE);
    wr16(elf, 56, 1);
    wr32(elf, phoff, PT_LOAD);
    wr32(elf, phoff + 4u, PF_R | PF_X);
    wr64(elf, phoff + 8u, off);
    wr64(elf, phoff + 16u, load_addr);
    wr64(elf, phoff + 24u, load_addr);
    wr64(elf, phoff + 32u, filesz);
    wr64(elf, phoff + 40u, memsz);
    wr64(elf, phoff + 48u, 4096u);
    *entry_out = entry;
    return (int)(off + filesz);
}

int chrisld_link(const ChrisoImage *img, uint64_t load_addr, void *out,
                 uint32_t cap, uint64_t *entry_out) {
    const ChrisoImage *one = img;
    if (!img) {
        return -1;
    }
    return chrisld_link_objects(&one, 1u, load_addr, out, cap, entry_out);
}

int chrisld_validate(const void *elfp, uint32_t n) {
    const uint8_t *elf = elfp;
    uint16_t phnum;
    uint16_t phentsize;
    uint64_t entry;
    uint32_t i;
    int entry_ok = 0;

    if (!elf || n < ELF_EHDR_SIZE) {
        return -1;
    }
    if (elf[0] != ELF_MAGIC_0 || elf[1] != ELF_MAGIC_1 ||
        elf[2] != ELF_MAGIC_2 || elf[3] != ELF_MAGIC_3) {
        return -1;
    }
    if (elf[4] != ELF_CLASS_64 || elf[5] != ELF_DATA_LSB) {
        return -1;
    }
    if ((uint16_t)(elf[18] | (elf[19] << 8)) != ELF_MACHINE_X86_64) {
        return -1;
    }
    phentsize = (uint16_t)(elf[54] | (elf[55] << 8));
    phnum = (uint16_t)(elf[56] | (elf[57] << 8));
    if (phentsize != ELF_PHDR_SIZE || phnum == 0) {
        return -1;
    }
    entry = rd64(elf, 24);
    for (i = 0; i < phnum; i++) {
        uint32_t ph = (uint32_t)rd64(elf, 32) + i * phentsize;
        uint32_t type;
        uint32_t flags;
        uint64_t vaddr, filesz, memsz;
        uint32_t k;
        if (ph + ELF_PHDR_SIZE > n) {
            return -1;
        }
        type = rd32(elf, ph);
        flags = rd32(elf, ph + 4u);
        vaddr = rd64(elf, ph + 16u);
        filesz = rd64(elf, ph + 32u);
        memsz = rd64(elf, ph + 40u);
        if (type != PT_LOAD) {
            continue;
        }
        if (filesz > memsz) {
            return -1;
        }
        if ((flags & PF_W) && (flags & PF_X)) {
            return -1;
        }
        if ((flags & PF_X) && entry >= vaddr && entry < vaddr + memsz) {
            entry_ok = 1;
        }
        for (k = i + 1u; k < phnum; k++) {
            uint32_t ph2 = (uint32_t)rd64(elf, 32) + k * phentsize;
            uint64_t v2, m2;
            if (rd32(elf, ph2) != PT_LOAD) {
                continue;
            }
            v2 = rd64(elf, ph2 + 16u);
            m2 = rd64(elf, ph2 + 40u);
            if (vaddr < v2 + m2 && v2 < vaddr + memsz) {
                return -1;
            }
        }
    }
    return entry_ok ? 0 : -1;
}
