#include "mm.h"
#include "bootinfo.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"

#define MM_PS        (1ull << 7)
#define MM_ADDR_MASK 0x000ffffffffff000ull
#define MMIO_WINDOW  0xffffffff90000000ull
#define MM_TEST_VIRT 0xffffffff91000000ull
#define LAPIC_PHYS   0x00000000fee00000ull

static uint64_t mm_cr3_phys;
static uint64_t mmio_next;
static void *mm_lapic_mapped;
static int mm_ready;

static unsigned pml4_index(uint64_t virt) {
    return (unsigned)((virt >> 39) & 0x1ffull);
}

static unsigned pdpt_index(uint64_t virt) {
    return (unsigned)((virt >> 30) & 0x1ffull);
}

static unsigned pd_index(uint64_t virt) {
    return (unsigned)((virt >> 21) & 0x1ffull);
}

static unsigned pt_index(uint64_t virt) {
    return (unsigned)((virt >> 12) & 0x1ffull);
}

static uint64_t *table_from_phys(uint64_t phys) {
    return (uint64_t *)bootinfo_phys_to_virt(phys & MM_ADDR_MASK);
}

static uint64_t alloc_zero_table(void) {
    uint64_t phys;
    uint64_t *entries;
    uint64_t index;

    phys = pmm_alloc();
    if (phys == 0) {
        panic("mm: pmm_alloc de tabela falhou");
    }
    entries = table_from_phys(phys);
    for (index = 0; index < 512; ++index) {
        entries[index] = 0;
    }
    return phys;
}

static uint64_t *ensure_table(uint64_t *parent, unsigned index) {
    uint64_t entry;
    uint64_t child;

    entry = parent[index];
    if ((entry & MM_PRESENT) == 0) {
        child = alloc_zero_table();
        parent[index] = child | MM_PRESENT | MM_WRITE;
        return table_from_phys(child);
    }
    if ((entry & MM_PS) != 0) {
        panic("mm: huge page no caminho de map_4k");
    }
    return table_from_phys(entry);
}

static void map_4k_ex(uint64_t virt, uint64_t phys, uint64_t flags, int shootdown) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (!mm_ready) {
        panic("map_4k antes de mm_init");
    }
    if ((virt & (PMM_PAGE - 1ull)) != 0 || (phys & (PMM_PAGE - 1ull)) != 0) {
        panic("map_4k desalinhado");
    }

    pml4 = table_from_phys(mm_cr3_phys);
    pdpt = ensure_table(pml4, pml4_index(virt));
    pd = ensure_table(pdpt, pdpt_index(virt));
    pt = ensure_table(pd, pd_index(virt));
    pt[pt_index(virt)] = (phys & MM_ADDR_MASK) | (flags | MM_PRESENT);
    if (shootdown) {
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

void map_4k(uint64_t virt, uint64_t phys, uint64_t flags) {
    map_4k_ex(virt, phys, flags, 1);
}

void map_4k_nosync(uint64_t virt, uint64_t phys, uint64_t flags) {
    map_4k_ex(virt, phys, flags, 0);
}

void mm_flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void *mm_lapic_virt(void) {
    return mm_lapic_mapped;
}

void *map_mmio_page(uint64_t phys) {
    uint64_t virt;

    if ((phys & (PMM_PAGE - 1ull)) != 0) {
        panic("map_mmio_page desalinhado");
    }
    virt = mmio_next;
    mmio_next += PMM_PAGE;
    map_4k(virt, phys, MM_PRESENT | MM_WRITE | MM_PWT | MM_PCD | MM_NX);
    return (void *)virt;
}

uint64_t mm_virt_to_phys(uint64_t virt) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t entry;

    if (!mm_ready) {
        panic("mm_virt_to_phys antes de mm_init");
    }

    pml4 = table_from_phys(mm_cr3_phys);
    entry = pml4[pml4_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        return 0;
    }

    pdpt = table_from_phys(entry);
    entry = pdpt[pdpt_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        return 0;
    }
    if ((entry & MM_PS) != 0) {
        return (entry & 0x000fffffc0000000ull) | (virt & 0x3fffffffull);
    }

    pd = table_from_phys(entry);
    entry = pd[pd_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        return 0;
    }
    if ((entry & MM_PS) != 0) {
        return (entry & 0x000fffffffe00000ull) | (virt & 0x1fffffull);
    }

    pt = table_from_phys(entry);
    entry = pt[pt_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        return 0;
    }
    return (entry & MM_ADDR_MASK) | (virt & 0xfffull);
}

void mm_init(void) {
    const struct bootinfo *boot;
    uint64_t cr3;
    uint64_t fb_phys;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    mm_cr3_phys = cr3 & MM_ADDR_MASK;
    mmio_next = MMIO_WINDOW;
    mm_ready = 1;

    boot = bootinfo_get();
    fb_phys = mm_virt_to_phys(boot->fb_addr);

    serial_puts("mm CR3 phys=");
    serial_write_hex(mm_cr3_phys);
    serial_puts("\nmm fb virt=");
    serial_write_hex(boot->fb_addr);
    serial_puts(" phys=");
    serial_write_hex(fb_phys);
    serial_puts("\n");
    if (fb_phys == 0) {
        panic("mm: framebuffer sem traducao");
    }
}

void mm_selftest(void) {
    uint64_t phys;
    volatile uint32_t *alias;
    volatile uint32_t *via_hhdm;
    volatile uint32_t *lapic;
    uint32_t lapic_id;
    const struct bootinfo *boot;

    phys = pmm_alloc();
    if (phys == 0) {
        panic("mm_selftest: pmm_alloc falhou");
    }
    map_4k(MM_TEST_VIRT, phys, MM_PRESENT | MM_WRITE | MM_NX);
    alias = (volatile uint32_t *)MM_TEST_VIRT;
    via_hhdm = (volatile uint32_t *)bootinfo_phys_to_virt(phys);
    *alias = 0x00c0ffeeu;
    if (*via_hhdm != 0x00c0ffeeu) {
        panic("mm_selftest: alias 4K falhou");
    }
    serial_puts("mm: alias 4K OK phys=");
    serial_write_hex(phys);
    serial_puts("\n");

    lapic = (volatile uint32_t *)map_mmio_page(LAPIC_PHYS);
    mm_lapic_mapped = (void *)lapic;
    lapic_id = lapic[0x20 / 4] >> 24;
    boot = bootinfo_get();
    serial_puts("mm: lapic virt=");
    serial_write_hex((uint64_t)lapic);
    serial_puts(" id=");
    serial_write_u64(lapic_id);
    serial_puts(" bsp_lapic_id=");
    serial_write_u64(boot->bsp_lapic_id);
    serial_puts("\n");
}
