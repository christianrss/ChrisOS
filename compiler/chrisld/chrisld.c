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

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

int chrisld_link(const ChrisoImage *img, uint64_t load_addr, void *out,
                 uint32_t cap, uint64_t *entry_out) {
    uint8_t *elf = out;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t off;
    uint32_t phoff;
    uint32_t i;
    uint64_t entry = load_addr;

    if (!img || !out || !entry_out) {
        return -1;
    }
    if (!img->sec[CHRISO_SEC_TEXT] || img->sec_size[CHRISO_SEC_TEXT] == 0u) {
        return -1;
    }
    for (i = 0; i < img->nsym; i++) {
        if (img->sym[i].name[0] == 'k' && img->sym[i].name[1] == 's' &&
            img->sym[i].name[2] == 't' && img->sym[i].name[3] == 'a' &&
            img->sym[i].name[4] == 'r' && img->sym[i].name[5] == 't') {
            entry = load_addr + img->sym[i].offset;
            break;
        }
        if (img->sym[i].name[0] == 'm' && img->sym[i].name[1] == 'a' &&
            img->sym[i].name[2] == 'i' && img->sym[i].name[3] == 'n') {
            entry = load_addr + img->sym[i].offset;
        }
    }
    phoff = ELF_EHDR_SIZE;
    off = ELF_EHDR_SIZE + ELF_PHDR_SIZE;
    off = align_up(off, 16u);
    filesz = img->sec_size[CHRISO_SEC_TEXT];
    memsz = align_up(filesz, 4096u);
    if ((uint64_t)off + filesz > cap) {
        return -1;
    }
    memset(elf, 0, off + filesz);
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
    wr64(elf, 40, 0);
    wr16(elf, 48, 0);
    wr16(elf, 50, ELF_EHDR_SIZE);
    wr16(elf, 52, ELF_PHDR_SIZE);
    wr16(elf, 54, 1);
    wr16(elf, 56, 64);
    wr16(elf, 58, 56);

    wr32(elf, phoff, PT_LOAD);
    wr32(elf, phoff + 4u, PF_R | PF_X);
    wr64(elf, phoff + 8u, off);
    wr64(elf, phoff + 16u, load_addr);
    wr64(elf, phoff + 24u, load_addr);
    wr64(elf, phoff + 32u, filesz);
    wr64(elf, phoff + 40u, memsz);
    wr64(elf, phoff + 48u, 4096u);

    memcpy(elf + off, img->sec[CHRISO_SEC_TEXT], filesz);
    *entry_out = entry;
    return (int)(off + filesz);
}
