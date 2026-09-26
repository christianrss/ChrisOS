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

typedef struct LdMap {
    uint32_t at[4][LD_OBJS];
    uint32_t total[4];
    uint32_t ro_off;
    uint32_t rx_filesz;
    uint32_t rx_memsz;
    uint32_t rx_file;
    uint32_t rw_file;
    uint64_t rw_vaddr;
    int two;
} LdMap;

static uint64_t sym_addr(uint64_t load, const LdMap *map, uint32_t obj,
                         const ChrisoSym *s) {
    if (s->section == CHRISO_SEC_RODATA) {
        return load + map->ro_off + map->at[CHRISO_SEC_RODATA][obj] + s->offset;
    }
    if (s->section == CHRISO_SEC_DATA) {
        return map->rw_vaddr + map->at[CHRISO_SEC_DATA][obj] + s->offset;
    }
    if (s->section == CHRISO_SEC_BSS) {
        return map->rw_vaddr + map->total[CHRISO_SEC_DATA] +
               map->at[CHRISO_SEC_BSS][obj] + s->offset;
    }
    return load + map->at[CHRISO_SEC_TEXT][obj] + s->offset;
}

static int apply_one(uint8_t *elf, uint32_t elf_len, uint32_t file_at,
                     uint64_t place, uint64_t s_addr, const ChrisoRel *rel) {
    uint64_t value = s_addr + (uint64_t)(int64_t)rel->addend;
    int32_t disp;

    if (rel->type == R_X86_64_NONE) {
        return 0;
    }
    if (rel->type == R_X86_64_64) {
        if ((uint64_t)file_at + 8u > elf_len) {
            return -1;
        }
        wr64(elf, file_at, value);
        return 0;
    }
    if (rel->type == R_X86_64_PC32 || rel->type == R_X86_64_PLT32) {
        int64_t d = (int64_t)value - (int64_t)place;
        if ((uint64_t)file_at + 4u > elf_len || d > 2147483647ll || d < -2147483648ll) {
            return -1;
        }
        disp = (int32_t)d;
        wr32(elf, file_at, (uint32_t)disp);
        return 0;
    }
    if (rel->type == R_X86_64_32 || rel->type == R_X86_64_32S) {
        if ((uint64_t)file_at + 4u > elf_len || value > 0xffffffffull) {
            return -1;
        }
        wr32(elf, file_at, (uint32_t)value);
        return 0;
    }
    return -1;
}

static int reloc_site(const LdMap *map, uint64_t load, uint32_t obj,
                      const ChrisoRel *rel, uint32_t *file_at, uint64_t *place) {
    uint32_t off = rel->offset;
    if (rel->section == CHRISO_SEC_TEXT) {
        *place = load + map->at[CHRISO_SEC_TEXT][obj] + off;
        *file_at = map->rx_file + map->at[CHRISO_SEC_TEXT][obj] + off;
        return 0;
    }
    if (rel->section == CHRISO_SEC_RODATA) {
        *place = load + map->ro_off + map->at[CHRISO_SEC_RODATA][obj] + off;
        *file_at = map->rx_file + map->ro_off + map->at[CHRISO_SEC_RODATA][obj] + off;
        return 0;
    }
    if (rel->section == CHRISO_SEC_DATA) {
        *place = map->rw_vaddr + map->at[CHRISO_SEC_DATA][obj] + off;
        *file_at = map->rw_file + map->at[CHRISO_SEC_DATA][obj] + off;
        return 0;
    }
    return -1;
}

static void pack_sec(LdMap *map, const ChrisoImage *const *imgs, uint32_t n,
                     uint32_t sec) {
    uint32_t i;
    uint32_t total = 0;
    for (i = 0; i < n; i++) {
        uint32_t sz = imgs[i]->sec_size[sec];
        map->at[sec][i] = total;
        if (sz > 0u) {
            if (total != 0u) {
                map->at[sec][i] = align_up(total, 16u);
            }
            total = map->at[sec][i] + sz;
        }
    }
    map->total[sec] = total;
}

static void wr_phdr(uint8_t *elf, uint32_t ph, uint32_t flags, uint64_t off,
                    uint64_t vaddr, uint64_t filesz, uint64_t memsz) {
    wr32(elf, ph, PT_LOAD);
    wr32(elf, ph + 4u, flags);
    wr64(elf, ph + 8u, off);
    wr64(elf, ph + 16u, vaddr);
    wr64(elf, ph + 24u, vaddr);
    wr64(elf, ph + 32u, filesz);
    wr64(elf, ph + 40u, memsz);
    wr64(elf, ph + 48u, 4096u);
}

int chrisld_link_objects(const ChrisoImage *const *imgs, uint32_t n,
                         uint64_t load_addr, void *out, uint32_t cap,
                         uint64_t *entry_out) {
    uint8_t *elf = out;
    LdMap map;
    uint32_t phoff;
    uint32_t phnum;
    uint32_t file_end;
    uint32_t i;
    uint32_t j;
    uint32_t sec;
    uint64_t entry = load_addr;
    int saw_entry = 0;

    if (!imgs || !n || n > LD_OBJS || !out || !entry_out) {
        return -1;
    }
    memset(&map, 0, sizeof(map));
    if (duplicate_globals(imgs, n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (!imgs[i]) {
            return -1;
        }
    }
    for (sec = 0; sec < 4u; sec++) {
        pack_sec(&map, imgs, n, sec);
    }
    map.ro_off = map.total[CHRISO_SEC_RODATA] ? align_up(map.total[CHRISO_SEC_TEXT], 16u)
                                             : map.total[CHRISO_SEC_TEXT];
    map.rx_filesz = map.total[CHRISO_SEC_RODATA] ? map.ro_off + map.total[CHRISO_SEC_RODATA]
                                                : map.total[CHRISO_SEC_TEXT];
    map.two = map.total[CHRISO_SEC_DATA] > 0u || map.total[CHRISO_SEC_BSS] > 0u;
    if (map.rx_filesz == 0u && !map.two) {
        return -1;
    }
    phoff = ELF_EHDR_SIZE;
    phnum = map.two ? 2u : 1u;
    if (!map.two) {
        map.rx_file = align_up(ELF_EHDR_SIZE + ELF_PHDR_SIZE, 16u);
        map.rx_memsz = align_up(map.rx_filesz, 4096u);
        file_end = map.rx_file + map.rx_filesz;
    } else {
        uint32_t page = (uint32_t)(load_addr & 0xfffu);
        map.rx_file = 4096u + page;
        map.rx_memsz = align_up(map.rx_filesz, 4096u);
        map.rw_vaddr = load_addr + map.rx_memsz;
        if (map.total[CHRISO_SEC_DATA] > 0u) {
            map.rw_file = map.rx_file + map.rx_memsz;
            file_end = map.rw_file + map.total[CHRISO_SEC_DATA];
        } else {
            map.rw_file = 0;
            file_end = map.rx_file + map.rx_filesz;
        }
    }
    if (file_end > cap) {
        return -1;
    }
    memset(elf, 0, file_end);
    for (i = 0; i < n; i++) {
        if (imgs[i]->sec_size[CHRISO_SEC_TEXT] > 0 && imgs[i]->sec[CHRISO_SEC_TEXT]) {
            memcpy(elf + map.rx_file + map.at[CHRISO_SEC_TEXT][i],
                   imgs[i]->sec[CHRISO_SEC_TEXT], imgs[i]->sec_size[CHRISO_SEC_TEXT]);
        }
        if (imgs[i]->sec_size[CHRISO_SEC_RODATA] > 0 && imgs[i]->sec[CHRISO_SEC_RODATA]) {
            memcpy(elf + map.rx_file + map.ro_off + map.at[CHRISO_SEC_RODATA][i],
                   imgs[i]->sec[CHRISO_SEC_RODATA], imgs[i]->sec_size[CHRISO_SEC_RODATA]);
        }
        if (map.two && imgs[i]->sec_size[CHRISO_SEC_DATA] > 0 &&
            imgs[i]->sec[CHRISO_SEC_DATA]) {
            memcpy(elf + map.rw_file + map.at[CHRISO_SEC_DATA][i],
                   imgs[i]->sec[CHRISO_SEC_DATA], imgs[i]->sec_size[CHRISO_SEC_DATA]);
        }
        for (j = 0; j < imgs[i]->nsym; j++) {
            const ChrisoSym *s = &imgs[i]->sym[j];
            if (!defined_sym(s)) {
                continue;
            }
            if (!saw_entry && same_name(s->name, "kstart")) {
                entry = sym_addr(load_addr, &map, i, s);
                saw_entry = 1;
            }
        }
    }
    if (!saw_entry) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < imgs[i]->nsym; j++) {
                const ChrisoSym *s = &imgs[i]->sym[j];
                if (defined_sym(s) && same_name(s->name, "main")) {
                    entry = sym_addr(load_addr, &map, i, s);
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
            uint32_t file_at = 0;
            uint64_t place = 0;
            if (rel->sym_index >= imgs[i]->nsym) {
                return -1;
            }
            sym = &imgs[i]->sym[rel->sym_index];
            def = resolve_sym(imgs, n, i, sym, &def_obj);
            if (!def) {
                return -1;
            }
            if (reloc_site(&map, load_addr, i, rel, &file_at, &place) != 0) {
                return -1;
            }
            if (apply_one(elf, file_end, file_at, place,
                          sym_addr(load_addr, &map, def_obj, def), rel) != 0) {
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
    wr16(elf, 56, (uint16_t)phnum);
    wr_phdr(elf, phoff, PF_R | PF_X, map.rx_file, load_addr, map.rx_filesz, map.rx_memsz);
    if (map.two) {
        uint64_t rw_mem = (uint64_t)map.total[CHRISO_SEC_DATA] + map.total[CHRISO_SEC_BSS];
        wr_phdr(elf, phoff + ELF_PHDR_SIZE, PF_R | PF_W, map.rw_file, map.rw_vaddr,
                map.total[CHRISO_SEC_DATA], rw_mem);
    }
    *entry_out = entry;
    return (int)file_end;
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
