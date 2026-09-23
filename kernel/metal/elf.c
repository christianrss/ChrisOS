#include "elf.h"

#include "bootinfo.h"
#include "mm.h"
#include "pmm.h"
#include "syscall.h"
#include "proc.h"

#define ELF_MAGIC0 0x7fu
#define ELF_CLASS64 2u
#define ELF_DATA2LSB 1u
#define ELF_TYPE_EXEC 2u
#define ELF_MACHINE_X86_64 62u
#define PT_LOAD 1u

#define USER_LOAD_LO 0x400000ull
#define USER_LOAD_HI 0x500000ull

static uint32_t elf_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t elf_u64(const uint8_t *p) {
    return (uint64_t)elf_u32(p) |
           ((uint64_t)elf_u32(p + 4) << 32);
}

static uint16_t elf_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int elf_check_header(const uint8_t *file, uint32_t nbytes) {
    if (nbytes < 64u) {
        return -1;
    }
    if (file[0] != ELF_MAGIC0 || file[1] != 'E' || file[2] != 'L' || file[3] != 'F') {
        return -1;
    }
    if (file[4] != ELF_CLASS64 || file[5] != ELF_DATA2LSB) {
        return -1;
    }
    if (elf_u16(file + 16) != ELF_TYPE_EXEC) {
        return -1;
    }
    if (elf_u16(file + 18) != ELF_MACHINE_X86_64) {
        return -1;
    }
    if (elf_u32(file + 20) != 1u) {
        return -1;
    }
    return 0;
}

static int g_elf_pid;

static void elf_zero_user(uint64_t virt, uint64_t phys, uint64_t page_flags) {
    uint64_t *words;
    uint64_t index;

    words = (uint64_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
    for (index = 0; index < PMM_PAGE / sizeof(uint64_t); ++index) {
        words[index] = 0;
    }
    if (proc_map_user(g_elf_pid, virt, phys, page_flags) != 0) {
        map_4k(virt, phys, page_flags);
    }
}

static int elf_map_segment(const uint8_t *file, uint32_t nbytes,
                           uint64_t vaddr, uint64_t offset,
                           uint64_t filesz, uint64_t memsz, uint32_t flags) {
    uint64_t page_flags;
    uint64_t page_start;
    uint64_t page_end;
    uint64_t virt;
    uint64_t phys;
    uint64_t copy_len;
    uint8_t *dst;

    if (vaddr < USER_LOAD_LO || vaddr + memsz > USER_LOAD_HI) {
        return -1;
    }
    if (offset + filesz > (uint64_t)nbytes) {
        return -1;
    }

    page_flags = MM_PRESENT | MM_USER;
    if ((flags & 2u) != 0) {
        page_flags |= MM_WRITE;
    }
    if ((flags & 1u) == 0) {
        page_flags |= MM_NX;
    }

    page_start = vaddr & ~(PMM_PAGE - 1ull);
    page_end = (vaddr + memsz + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);

    for (virt = page_start; virt < page_end; virt += PMM_PAGE) {
        uint64_t page_off;
        phys = pmm_alloc();
        if (phys == 0) {
            return -1;
        }
        elf_zero_user(virt, phys, page_flags);
        if (virt + PMM_PAGE <= vaddr || virt >= vaddr + filesz) {
            continue;
        }
        page_off = virt < vaddr ? 0 : virt - vaddr;
        dst = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
        if (virt < vaddr) {
            dst += (uint32_t)(vaddr - virt);
        }
        copy_len = PMM_PAGE;
        if (virt < vaddr) {
            copy_len = PMM_PAGE - (vaddr - virt);
        }
        if (page_off + copy_len > filesz) {
            copy_len = filesz - page_off;
        }
        {
            uint64_t i;
            for (i = 0; i < copy_len; ++i) {
                dst[i] = file[offset + page_off + i];
            }
        }
    }

    syscall_set_user_map(USER_LOAD_LO, USER_LOAD_HI);
    return 0;
}

int elf_load(const uint8_t *file, uint32_t nbytes, uint64_t *entry_out) {
    uint64_t phoff;
    uint16_t phnum;
    uint16_t phentsize;
    uint16_t index;
    uint64_t entry;

    if (!file || !entry_out) {
        return -1;
    }
    if (elf_check_header(file, nbytes) != 0) {
        return -1;
    }

    entry = elf_u64(file + 24);
    phoff = elf_u64(file + 32);
    phentsize = elf_u16(file + 54);
    phnum = elf_u16(file + 56);

    if (phentsize < 56u || phnum == 0u || phoff + (uint64_t)phentsize * phnum > nbytes) {
        return -1;
    }
    if (entry < USER_LOAD_LO || entry >= USER_LOAD_HI) {
        return -1;
    }

    g_elf_pid = proc_create("elf");
    if (g_elf_pid < 0) {
        return -1;
    }
    proc_switch(g_elf_pid);

    for (index = 0; index < phnum; ++index) {
        const uint8_t *ph = file + phoff + (uint64_t)index * (uint64_t)phentsize;
        uint32_t type = elf_u32(ph);
        uint32_t flags = elf_u32(ph + 4);
        uint64_t offset = elf_u64(ph + 8);
        uint64_t vaddr = elf_u64(ph + 16);
        uint64_t filesz = elf_u64(ph + 32);
        uint64_t memsz = elf_u64(ph + 40);

        if (type != PT_LOAD) {
            if (type == 0u) {
                continue;
            }
            return -1;
        }
        if (elf_map_segment(file, nbytes, vaddr, offset, filesz, memsz, flags) != 0) {
            return -1;
        }
    }

    *entry_out = entry;
    return 0;
}
