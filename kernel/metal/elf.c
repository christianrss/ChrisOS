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
#define PT_NULL 0u
#define PT_LOAD 1u
#define PF_X 1u
#define PF_W 2u

#define USER_LOAD_LO 0x400000ull
#define USER_LOAD_HI 0x500000ull
#define ELF_MAX_PH 32

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

static int span_fits(uint64_t addr, uint64_t len, uint64_t limit) {
    if (len > limit) {
        return -1;
    }
    if (addr > limit - len) {
        return -1;
    }
    return 0;
}

static int elf_map_segment(int pid, const uint8_t *file, uint32_t nbytes,
                           uint64_t vaddr, uint64_t offset,
                           uint64_t filesz, uint64_t memsz, uint32_t flags) {
    uint64_t page_flags;
    uint64_t page_start;
    uint64_t page_end;
    uint64_t virt;
    uint64_t phys;

    (void)nbytes;
    page_flags = MM_PRESENT | MM_USER;
    if ((flags & PF_W) != 0) {
        page_flags |= MM_WRITE;
    }
    if ((flags & PF_X) == 0) {
        page_flags |= MM_NX;
    }

    page_start = vaddr & ~(PMM_PAGE - 1ull);
    page_end = (vaddr + memsz + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);

    for (virt = page_start; virt < page_end; virt += PMM_PAGE) {
        uint64_t page_off;
        uint64_t copy_len;
        uint8_t *dst;
        phys = pmm_alloc();
        if (phys == 0) {
            return -1;
        }
        dst = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
        {
            uint64_t i;
            for (i = 0; i < PMM_PAGE; ++i) {
                dst[i] = 0;
            }
        }
        if (proc_map_owned(pid, virt, phys, page_flags) != 0) {
            pmm_free(phys);
            return -1;
        }
        if (filesz == 0 || virt + PMM_PAGE <= vaddr || virt >= vaddr + filesz) {
            continue;
        }
        page_off = virt < vaddr ? 0 : virt - vaddr;
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
    return 0;
}

typedef struct ElfSeg {
    uint64_t vaddr;
    uint64_t offset;
    uint64_t filesz;
    uint64_t memsz;
    uint32_t flags;
} ElfSeg;

static int segs_overlap(const ElfSeg *a, const ElfSeg *b) {
    uint64_t a_end = a->vaddr + a->memsz;
    uint64_t b_end = b->vaddr + b->memsz;
    if (a->vaddr >= b_end || b->vaddr >= a_end) {
        return 0;
    }
    return 1;
}

static int elf_check_segment(const uint8_t *file, uint32_t nbytes, const ElfSeg *seg) {
    uint64_t page_start;
    uint64_t page_end;
    (void)file;
    if (seg->memsz == 0) {
        return -1;
    }
    if (seg->filesz > seg->memsz) {
        return -1;
    }
    if ((seg->flags & PF_X) != 0 && (seg->flags & PF_W) != 0) {
        return -1;
    }
    if (seg->vaddr > UINT64_MAX - seg->memsz) {
        return -1;
    }
    if (seg->filesz > 0 && span_fits(seg->offset, seg->filesz, nbytes) != 0) {
        return -1;
    }
    if ((seg->vaddr & (PMM_PAGE - 1ull)) != (seg->offset & (PMM_PAGE - 1ull))) {
        return -1;
    }
    page_start = seg->vaddr & ~(PMM_PAGE - 1ull);
    if (seg->memsz > UINT64_MAX - (PMM_PAGE - 1ull) - seg->vaddr) {
        return -1;
    }
    page_end = (seg->vaddr + seg->memsz + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
    if (page_start < USER_LOAD_LO || page_end > USER_LOAD_HI || page_end < page_start) {
        return -1;
    }
    return 0;
}

static void elf_abort(int pid, int prev) {
    proc_destroy(pid);
    if (prev >= 0 && prev != pid) {
        proc_switch(prev);
    }
}

int elf_load(const uint8_t *file, uint32_t nbytes, uint64_t *entry_out) {
    uint64_t phoff;
    uint16_t phnum;
    uint16_t phentsize;
    uint16_t index;
    uint64_t entry;
    ElfSeg segs[ELF_MAX_PH];
    int nseg = 0;
    int exec_hit = 0;
    int pid;
    int prev;

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

    if (phentsize < 56u || phnum == 0u || phnum > ELF_MAX_PH) {
        return -1;
    }
    if (span_fits(phoff, (uint64_t)phentsize * (uint64_t)phnum, nbytes) != 0) {
        return -1;
    }
    if (entry < USER_LOAD_LO || entry >= USER_LOAD_HI) {
        return -1;
    }

    for (index = 0; index < phnum; ++index) {
        const uint8_t *ph = file + phoff + (uint64_t)index * (uint64_t)phentsize;
        uint32_t type = elf_u32(ph);
        ElfSeg seg;

        if (type == PT_NULL) {
            continue;
        }
        if (type != PT_LOAD) {
            return -1;
        }
        seg.flags = elf_u32(ph + 4);
        seg.offset = elf_u64(ph + 8);
        seg.vaddr = elf_u64(ph + 16);
        seg.filesz = elf_u64(ph + 32);
        seg.memsz = elf_u64(ph + 40);
        if (elf_check_segment(file, nbytes, &seg) != 0) {
            return -1;
        }
        {
            int s;
            for (s = 0; s < nseg; ++s) {
                if (segs_overlap(&segs[s], &seg)) {
                    return -1;
                }
            }
        }
        if ((seg.flags & PF_X) != 0 && entry >= seg.vaddr &&
            seg.memsz > entry - seg.vaddr) {
            exec_hit = 1;
        }
        segs[nseg++] = seg;
    }
    if (nseg == 0 || !exec_hit) {
        return -1;
    }

    prev = proc_current();
    pid = proc_create("elf");
    if (pid < 0) {
        return -1;
    }
    proc_switch(pid);

    for (index = 0; index < (uint16_t)nseg; ++index) {
        ElfSeg *seg = &segs[index];
        if (elf_map_segment(pid, file, nbytes, seg->vaddr, seg->offset,
                            seg->filesz, seg->memsz, seg->flags) != 0) {
            elf_abort(pid, prev);
            return -1;
        }
    }

    syscall_set_user_map(USER_LOAD_LO, USER_LOAD_HI);
    *entry_out = entry;
    return 0;
}
