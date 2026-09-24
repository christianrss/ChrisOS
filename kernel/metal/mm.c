#include "mm.h"
#include "apic.h"
#include "smp.h"
#include "bootinfo.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"
#include "spin.h"

#define MM_PS        (1ull << 7)
#define MM_ADDR_MASK 0x000ffffffffff000ull
#define MMIO_WINDOW  0xffffffff90000000ull
#define MM_TEST_VIRT 0xffffffff91000000ull
#define LAPIC_PHYS   0x00000000fee00000ull

static uint64_t mm_cr3_phys;
static uint64_t mmio_next;
static void *mm_lapic_mapped;
static int mm_ready;
static Spinlock mm_lock;
static volatile uint64_t mm_tlb_gen;
static volatile uint64_t mm_tlb_seen[SMP_CPU_CAP];

#define MMIO_LIMIT (MMIO_WINDOW + 256ull * PMM_PAGE)

static void mm_enter(void) {
    for (;;) {
        mm_tlb_poll();
        if (cas_u32(&mm_lock.locked, 0u, 1u)) {
            return;
        }
        __asm__ volatile ("pause");
    }
}

static void mm_leave(void) {
    spin_unlock(&mm_lock);
}

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

static uint64_t alloc_zero_table_try(void) {
    uint64_t phys;
    uint64_t *entries;
    uint64_t index;

    phys = pmm_alloc();
    if (phys == 0) {
        return 0;
    }
    entries = table_from_phys(phys);
    for (index = 0; index < 512; ++index) {
        entries[index] = 0;
    }
    return phys;
}

static uint64_t alloc_zero_table(void) {
    uint64_t phys = alloc_zero_table_try();
    if (phys == 0) {
        panic("mm: pmm_alloc de tabela falhou");
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

static uint64_t *ensure_table_flags(uint64_t *parent, unsigned index, uint64_t extra) {
    uint64_t entry;
    uint64_t child;

    entry = parent[index];
    if ((entry & MM_PRESENT) == 0) {
        child = alloc_zero_table_try();
        if (child == 0) {
            return 0;
        }
        parent[index] = child | MM_PRESENT | MM_WRITE | extra;
        return table_from_phys(child);
    }
    if ((entry & MM_PS) != 0) {
        panic("mm: huge page no caminho de map_4k");
    }
    if ((extra & MM_USER) != 0 && (entry & MM_USER) == 0) {
        parent[index] = entry | MM_USER;
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

    mm_enter();
    pml4 = table_from_phys(mm_cr3_phys);
    pdpt = ensure_table(pml4, pml4_index(virt));
    pd = ensure_table(pdpt, pdpt_index(virt));
    pt = ensure_table(pd, pd_index(virt));
    pt[pt_index(virt)] = (phys & MM_ADDR_MASK) | (flags | MM_PRESENT);
    mm_leave();
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

void mm_tlb_poll(void) {
    uint32_t cpu = smp_current_cpu();
    uint64_t gen;
    uint64_t cr3;

    if (cpu >= SMP_CPU_CAP) {
        cpu = 0u;
    }
    gen = mm_tlb_gen;
    if (mm_tlb_seen[cpu] == gen) {
        return;
    }
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
    mm_tlb_seen[cpu] = gen;
}

void mm_tlb_shootdown(void) {
    uint32_t self = smp_current_cpu();
    uint32_t online;
    uint32_t spins = 0u;
    uint64_t gen;

    if (self >= SMP_CPU_CAP) {
        self = 0u;
    }
    mm_enter();
    gen = mm_tlb_gen + 1u;
    mm_tlb_gen = gen;
    mm_tlb_seen[self] = gen;
    online = cpu_online_count;
    if (online > 1u) {
        uint32_t i;
        uint32_t n = online;
        if (n > SMP_CPU_CAP) {
            n = SMP_CPU_CAP;
        }
        for (i = 0u; i < n; i++) {
            int known = 0;
            uint32_t lapic;
            if (i == self) {
                continue;
            }
            lapic = smp_lapic_of(i, &known);
            if (!known) {
                continue;
            }
            (void)apic_ipi(lapic, 0xF0u);
        }
    }
    if (online < 1u) {
        online = 1u;
    }
    if (online > SMP_CPU_CAP) {
        online = SMP_CPU_CAP;
    }
    for (;;) {
        uint32_t i;
        uint32_t ready = 0u;
        for (i = 0u; i < online; i++) {
            if (mm_tlb_seen[i] == gen) {
                ready++;
            }
        }
        if (ready >= online) {
            break;
        }
        if (++spins > 100000000u) {
            panic("tlb shootdown timeout");
        }
        __asm__ volatile ("pause");
    }
    mm_leave();
}

void mm_tlb_shootdown_range(uint64_t virt, uint64_t bytes) {
    uint64_t page;
    uint64_t last;

    if (!mm_ready || bytes == 0u) {
        mm_tlb_shootdown();
        return;
    }
    if (virt > ~0ull - (bytes - 1ull)) {
        bytes = ~0ull - virt;
    }
    page = virt & ~(PMM_PAGE - 1ull);
    last = (virt + bytes - 1ull) & ~(PMM_PAGE - 1ull);
    for (;;) {
        __asm__ volatile ("invlpg (%0)" : : "r"(page) : "memory");
        if (page == last || page > ~0ull - PMM_PAGE) {
            break;
        }
        page += PMM_PAGE;
    }
    mm_tlb_shootdown();
}

void unmap_4k(uint64_t virt) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t entry;

    if (!mm_ready) {
        return;
    }
    if ((virt & (PMM_PAGE - 1ull)) != 0) {
        panic("unmap_4k desalinhado");
    }
    mm_enter();
    pml4 = table_from_phys(mm_cr3_phys);
    entry = pml4[pml4_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pdpt = table_from_phys(entry);
    entry = pdpt[pdpt_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pd = table_from_phys(entry);
    entry = pd[pd_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pt = table_from_phys(entry);
    pt[pt_index(virt)] = 0;
    mm_leave();
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void mm_unmap_cr3(uint64_t cr3_phys, uint64_t virt) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t entry;
    uint64_t cur;

    if (!mm_ready || cr3_phys == 0) {
        return;
    }
    if ((virt & (PMM_PAGE - 1ull)) != 0) {
        return;
    }
    mm_enter();
    pml4 = table_from_phys(cr3_phys);
    entry = pml4[pml4_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pdpt = table_from_phys(entry);
    entry = pdpt[pdpt_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pd = table_from_phys(entry);
    entry = pd[pd_index(virt)];
    if ((entry & MM_PRESENT) == 0 || (entry & MM_PS) != 0) {
        mm_leave();
        return;
    }
    pt = table_from_phys(entry);
    pt[pt_index(virt)] = 0;
    mm_leave();
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cur));
    if ((cur & MM_ADDR_MASK) == (cr3_phys & MM_ADDR_MASK)) {
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

static int translate_leaf(uint64_t table_phys, uint64_t virt, int level,
                          uint64_t *phys, uint64_t *flags) {
    uint64_t *table = table_from_phys(table_phys);
    unsigned index;
    uint64_t entry;

    if (level == 3) {
        index = pml4_index(virt);
    } else if (level == 2) {
        index = pdpt_index(virt);
    } else if (level == 1) {
        index = pd_index(virt);
    } else {
        index = pt_index(virt);
    }
    entry = table[index];
    if ((entry & MM_PRESENT) == 0) {
        return -1;
    }
    if ((entry & MM_PS) != 0 || level == 0) {
        uint64_t base = entry & MM_ADDR_MASK;
        if ((entry & MM_PS) != 0 && level == 2) {
            base = entry & 0x000fffffc0000000ull;
        } else if ((entry & MM_PS) != 0 && level == 1) {
            base = entry & 0x000fffffffe00000ull;
        }
        *phys = base | (virt & (PMM_PAGE - 1ull));
        if ((entry & MM_PS) != 0 && level == 2) {
            *phys = base | (virt & 0x3fffffffull);
        } else if ((entry & MM_PS) != 0 && level == 1) {
            *phys = base | (virt & 0x1fffffull);
        }
        *flags = entry;
        return 0;
    }
    return translate_leaf(entry, virt, level - 1, phys, flags);
}

int mm_translate(uint64_t cr3_phys, uint64_t virt, uint64_t *phys, uint64_t *flags) {
    int rc;
    if (!mm_ready || cr3_phys == 0 || phys == 0 || flags == 0) {
        return -1;
    }
    mm_enter();
    rc = translate_leaf(cr3_phys, virt, 3, phys, flags);
    mm_leave();
    return rc;
}

static void free_table_page(uint64_t table_phys, int level) {
    uint64_t *table;
    unsigned i;

    table = table_from_phys(table_phys);
    if (level > 1) {
        for (i = 0; i < 512u; i++) {
            uint64_t entry = table[i];
            if ((entry & MM_PRESENT) != 0 && (entry & MM_PS) == 0) {
                free_table_page(entry & MM_ADDR_MASK, level - 1);
            }
        }
    }
    pmm_free(table_phys);
}

void mm_free_user_space(uint64_t cr3_phys) {
    uint64_t *pml4;
    unsigned i;

    if (!mm_ready || cr3_phys == 0 || cr3_phys == mm_cr3_phys) {
        return;
    }
    mm_enter();
    pml4 = table_from_phys(cr3_phys);
    for (i = 0; i < 256u; i++) {
        uint64_t entry = pml4[i];
        if ((entry & MM_PRESENT) != 0 && (entry & MM_PS) == 0) {
            free_table_page(entry & MM_ADDR_MASK, 3);
        }
        pml4[i] = 0;
    }
    mm_leave();
    pmm_free(cr3_phys);
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
    mm_enter();
    if (mmio_next < MMIO_WINDOW || mmio_next + PMM_PAGE > MMIO_LIMIT) {
        mm_leave();
        panic("mmio window exhausted");
    }
    virt = mmio_next;
    mmio_next += PMM_PAGE;
    mm_leave();
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

    mm_enter();
    pml4 = table_from_phys(mm_cr3_phys);
    entry = pml4[pml4_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        mm_leave();
        return 0;
    }

    pdpt = table_from_phys(entry);
    entry = pdpt[pdpt_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        mm_leave();
        return 0;
    }
    if ((entry & MM_PS) != 0) {
        uint64_t phys = (entry & 0x000fffffc0000000ull) | (virt & 0x3fffffffull);
        mm_leave();
        return phys;
    }

    pd = table_from_phys(entry);
    entry = pd[pd_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        mm_leave();
        return 0;
    }
    if ((entry & MM_PS) != 0) {
        uint64_t phys = (entry & 0x000fffffffe00000ull) | (virt & 0x1fffffull);
        mm_leave();
        return phys;
    }

    pt = table_from_phys(entry);
    entry = pt[pt_index(virt)];
    if ((entry & MM_PRESENT) == 0) {
        mm_leave();
        return 0;
    }
    {
        uint64_t phys = (entry & MM_ADDR_MASK) | (virt & 0xfffull);
        mm_leave();
        return phys;
    }
}

uint64_t mm_kernel_cr3(void) {
    return mm_cr3_phys;
}

uint64_t mm_clone_kernel_space(void) {
    uint64_t phys;
    uint64_t *dst;
    uint64_t *src;
    unsigned i;

    if (!mm_ready) {
        return 0;
    }
    mm_enter();
    phys = alloc_zero_table();
    dst = table_from_phys(phys);
    src = table_from_phys(mm_cr3_phys);
    for (i = 256; i < 512; ++i) {
        dst[i] = src[i];
    }
    mm_leave();
    return phys;
}

void mm_switch(uint64_t cr3_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_phys) : "memory");
}

int mm_map_cr3(uint64_t cr3_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t extra;

    if (!mm_ready || cr3_phys == 0) {
        return -1;
    }
    if ((virt & (PMM_PAGE - 1ull)) != 0 || (phys & (PMM_PAGE - 1ull)) != 0) {
        return -1;
    }
    extra = (flags & MM_USER) ? MM_USER : 0;
    mm_enter();
    pml4 = table_from_phys(cr3_phys);
    pdpt = ensure_table_flags(pml4, pml4_index(virt), extra);
    if (!pdpt) {
        mm_leave();
        return -1;
    }
    pd = ensure_table_flags(pdpt, pdpt_index(virt), extra);
    if (!pd) {
        mm_leave();
        return -1;
    }
    pt = ensure_table_flags(pd, pd_index(virt), extra);
    if (!pt) {
        mm_leave();
        return -1;
    }
    pt[pt_index(virt)] = (phys & MM_ADDR_MASK) | (flags | MM_PRESENT);
    mm_leave();
    return 0;
}

void mm_init(void) {
    const struct bootinfo *boot;
    uint64_t cr3;
    uint64_t fb_phys;

    spin_init(&mm_lock);
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
