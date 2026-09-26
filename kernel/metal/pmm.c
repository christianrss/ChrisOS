#define LIMINE_API_REVISION 3
#include <limine.h>
#include "pmm.h"
#include "bootinfo.h"
#include "panic.h"
#include "serial.h"
#include "smp.h"
#include "spin.h"

#define PMM_PAGE_COUNT (PMM_MAX_PHYS / PMM_PAGE)
#define PMM_BITMAP_BYTES (PMM_PAGE_COUNT / 8ull)

extern char __kernel_start[];
extern char __kernel_end[];

static uint8_t pmm_bitmap[PMM_BITMAP_BYTES];
static uint64_t pmm_usable;
static uint64_t pmm_used;
static uint64_t pmm_free_count;
static uint64_t pmm_cursor;
#define PMM_DMA32_PAGES 16u
#define PMM_DMA32_LIMIT (4ull * 1024ull * 1024ull * 1024ull)
static uint64_t dma32_base;
static uint32_t dma32_free;
static void pmm_reserve_dma32(void);
static Spinlock pmm_lock;
static int pmm_depth[SMP_CPU_CAP];
static uint64_t pmm_irq_flags[SMP_CPU_CAP];

/* Same-CPU recursion: heap_init's free-run callback claims pages while the
 * scan already holds this lock. PMM never takes the heap lock.
 * Interrupts are off so an IRQ on this CPU cannot observe depth > 0 and
 * re-enter the bitmap. */
static void pmm_enter(void) {
    uint32_t cpu = smp_current_cpu();

    if (cpu >= SMP_CPU_CAP) {
        cpu = 0u;
    }
    if (pmm_depth[cpu] > 0) {
        pmm_depth[cpu] += 1;
        return;
    }
    pmm_irq_flags[cpu] = irq_save();
    spin_lock(&pmm_lock);
    pmm_depth[cpu] = 1;
}

static void pmm_leave(void) {
    uint32_t cpu = smp_current_cpu();
    uint64_t flags;

    if (cpu >= SMP_CPU_CAP) {
        cpu = 0u;
    }
    if (pmm_depth[cpu] > 1) {
        pmm_depth[cpu] -= 1;
        return;
    }
    pmm_depth[cpu] = 0;
    flags = pmm_irq_flags[cpu];
    spin_unlock(&pmm_lock);
    irq_restore(flags);
}

static int page_in_range(uint64_t phys) {
    if ((phys & (PMM_PAGE - 1ull)) != 0) {
        return 0;
    }
    if (phys >= PMM_MAX_PHYS) {
        return 0;
    }
    return 1;
}

static int bitmap_is_used(uint64_t phys) {
    uint64_t page;
    uint64_t byte;
    uint8_t bit;

    page = phys / PMM_PAGE;
    byte = page / 8ull;
    bit = (uint8_t)(page % 8ull);
    return (pmm_bitmap[byte] & (uint8_t)(1u << bit)) != 0;
}

static void bitmap_set_used(uint64_t phys) {
    uint64_t page;
    uint64_t byte;
    uint8_t bit;

    page = phys / PMM_PAGE;
    byte = page / 8ull;
    bit = (uint8_t)(page % 8ull);
    pmm_bitmap[byte] = (uint8_t)(pmm_bitmap[byte] | (uint8_t)(1u << bit));
}

static void bitmap_set_free(uint64_t phys) {
    uint64_t page;
    uint64_t byte;
    uint8_t bit;

    page = phys / PMM_PAGE;
    byte = page / 8ull;
    bit = (uint8_t)(page % 8ull);
    pmm_bitmap[byte] = (uint8_t)(pmm_bitmap[byte] & (uint8_t)~(1u << bit));
}

static void pmm_reserve_page(uint64_t phys) {
    if (!page_in_range(phys)) {
        return;
    }
    if (!bitmap_is_used(phys)) {
        bitmap_set_used(phys);
        if (pmm_free_count > 0) {
            pmm_free_count -= 1;
        }
    }
}

static void pmm_release_page(uint64_t phys) {
    if (!page_in_range(phys)) {
        return;
    }
    if (bitmap_is_used(phys)) {
        bitmap_set_free(phys);
        pmm_free_count += 1;
    }
}

static void mark_range_used(uint64_t base, uint64_t length) {
    uint64_t addr;
    uint64_t end;

    addr = base & ~(PMM_PAGE - 1ull);
    end = (base + length + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
    while (addr < end) {
        pmm_reserve_page(addr);
        addr += PMM_PAGE;
    }
}

static void mark_usable_free(uint64_t base, uint64_t length) {
    uint64_t addr;
    uint64_t end;

    addr = (base + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
    end = (base + length) & ~(PMM_PAGE - 1ull);
    while (addr < end) {
        pmm_release_page(addr);
        addr += PMM_PAGE;
    }
}

void pmm_init(void) {
    uint64_t index;

    spin_init(&pmm_lock);
    uint64_t base;
    uint64_t length;
    uint64_t type;
    uint64_t count;
    uint64_t addr;

    for (index = 0; index < PMM_BITMAP_BYTES; ++index) {
        pmm_bitmap[index] = 0xffu;
    }
    pmm_free_count = 0;
    pmm_used = 0;
    pmm_usable = 0;

    count = bootinfo_memmap_count();
    for (index = 0; index < count; ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (type == LIMINE_MEMMAP_USABLE) {
            mark_usable_free(base, length);
        }
    }

    for (addr = 0; addr < 0x100000ull; addr += PMM_PAGE) {
        pmm_reserve_page(addr);
    }

    for (index = 0; index < count; ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (type == LIMINE_MEMMAP_RESERVED ||
            type == LIMINE_MEMMAP_ACPI_NVS ||
            type == LIMINE_MEMMAP_BAD_MEMORY ||
            type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES ||
            type == LIMINE_MEMMAP_FRAMEBUFFER) {
            mark_range_used(base, length);
        }
    }

    for (index = 0; index < count; ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            mark_range_used(base, length);
        }
    }

    pmm_usable = pmm_free_count;
    pmm_used = 0;
    pmm_cursor = 0;

    serial_puts("pmm kernel_virt ");
    serial_write_hex((uint64_t)__kernel_start);
    serial_puts("..");
    serial_write_hex((uint64_t)__kernel_end);
    serial_puts("\npmm usable=");
    serial_write_u64(pmm_usable);
    serial_puts(" used=");
    serial_write_u64(pmm_used);
    serial_puts(" free=");
    serial_write_u64(pmm_free_count);
    serial_puts("\n");
    pmm_reserve_dma32();
}

static uint64_t claim_run(uint64_t start, uint64_t count) {
    uint64_t run;
    uint64_t phys;

    for (run = 0; run < count; ++run) {
        phys = start + run * PMM_PAGE;
        bitmap_set_used(phys);
        if (pmm_free_count > 0) {
            pmm_free_count -= 1u;
        }
        pmm_used += 1u;
    }
    pmm_cursor = start + count * PMM_PAGE;
    return start;
}

void pmm_foreach_free_run(int (*cb)(uint64_t phys, uint64_t pages, void *user),
                          void *user);
uint64_t pmm_claim_at(uint64_t phys, uint64_t pages);

static uint64_t scan_usable_for_run(uint64_t count, uint64_t from_phys) {
    uint64_t index;
    uint64_t n;
    uint64_t base;
    uint64_t length;
    uint64_t type;
    uint64_t addr;
    uint64_t end;
    uint64_t run_start;
    uint64_t run;

    n = bootinfo_memmap_count();
    for (index = 0; index < n; ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        addr = (base + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
        end = (base + length) & ~(PMM_PAGE - 1ull);
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }
        if (addr < from_phys) {
            addr = (from_phys + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
        }
        run = 0;
        run_start = addr;
        while (addr < end) {
            uint64_t page;
            uint64_t byte;

            /* Skip whole used bitmap bytes (8 pages) without per-page checks. */
            page = addr / PMM_PAGE;
            byte = page / 8ull;
            if ((page & 7ull) == 0ull && byte < PMM_BITMAP_BYTES &&
                pmm_bitmap[byte] == 0xFFu) {
                run = 0;
                addr += 8ull * PMM_PAGE;
                run_start = addr;
                continue;
            }
            if (!page_in_range(addr) || bitmap_is_used(addr)) {
                run = 0;
                run_start = addr + PMM_PAGE;
            } else {
                run += 1u;
                if (run == count) {
                    return claim_run(run_start, count);
                }
            }
            addr += PMM_PAGE;
        }
    }
    return 0;
}

struct contig_find {
    uint64_t need;
    uint64_t found;
};

static int contig_find_cb(uint64_t phys, uint64_t pages, void *user) {
    struct contig_find *f = (struct contig_find *)user;

    if (f == 0 || pages < f->need) {
        return 1;
    }
    f->found = phys;
    return 0;
}

uint64_t pmm_alloc(void) {
    uint64_t phys;

    pmm_enter();
    phys = scan_usable_for_run(1u, pmm_cursor);
    if (phys == 0) {
        phys = scan_usable_for_run(1u, 0);
    }
    pmm_leave();
    return phys;
}

static void pmm_reserve_dma32(void) {
    uint64_t phys;

    dma32_base = 0;
    dma32_free = 0;
    phys = scan_usable_for_run(PMM_DMA32_PAGES, 0);
    if (phys == 0 || phys + (uint64_t)PMM_DMA32_PAGES * PMM_PAGE > PMM_DMA32_LIMIT) {
        if (phys != 0) {
            pmm_free_contig(phys, PMM_DMA32_PAGES);
        }
        serial_puts("pmm dma32 miss\n");
        return;
    }
    dma32_base = phys;
    dma32_free = (1u << PMM_DMA32_PAGES) - 1u;
    serial_puts("pmm dma32 ");
    serial_write_hex(phys);
    serial_puts("\n");
}

uint64_t pmm_alloc_dma32(uint64_t pages) {
    uint32_t need;
    uint32_t i;

    if (dma32_base == 0 || pages == 0 || pages > PMM_DMA32_PAGES) {
        return 0;
    }
    need = (1u << pages) - 1u;
    pmm_enter();
    for (i = 0; i + (uint32_t)pages <= PMM_DMA32_PAGES; ++i) {
        uint32_t mask = need << i;
        if ((dma32_free & mask) == mask) {
            dma32_free &= ~mask;
            pmm_leave();
            return dma32_base + (uint64_t)i * PMM_PAGE;
        }
    }
    pmm_leave();
    return 0;
}

void pmm_free_dma32(uint64_t phys, uint64_t pages) {
    uint64_t index;

    if (!pmm_dma32_owns(phys) || pages == 0) {
        return;
    }
    index = (phys - dma32_base) / PMM_PAGE;
    if (index + pages > PMM_DMA32_PAGES) {
        return;
    }
    pmm_enter();
    dma32_free |= ((1u << pages) - 1u) << index;
    pmm_leave();
}

int pmm_dma32_owns(uint64_t phys) {
    return dma32_base != 0 && phys >= dma32_base &&
           phys < dma32_base + (uint64_t)PMM_DMA32_PAGES * PMM_PAGE;
}

uint64_t pmm_alloc_contig(uint64_t count) {
    struct contig_find find;
    uint64_t phys;

    if (count == 0) {
        return 0;
    }
    pmm_enter();
    /* First free run that fits — byte-skipped walk in foreach. */
    find.need = count;
    find.found = 0;
    pmm_foreach_free_run(contig_find_cb, &find);
    phys = 0;
    if (find.found != 0) {
        phys = pmm_claim_at(find.found, count);
    }
    if (phys == 0) {
        phys = scan_usable_for_run(count, 0);
    }
    pmm_leave();
    return phys;
}

void pmm_free(uint64_t phys) {
    pmm_free_contig(phys, 1u);
}

void pmm_free_contig(uint64_t phys, uint64_t count) {
    uint64_t run;

    if (phys == 0 || count == 0) {
        return;
    }
    pmm_enter();
    for (run = 0; run < count; ++run) {
        uint64_t page_phys = phys + run * PMM_PAGE;
        if (!page_in_range(page_phys)) {
            panic("pmm_free_contig desalinhado ou fora do PMM");
        }
        if (bitmap_is_used(page_phys)) {
            bitmap_set_free(page_phys);
            pmm_free_count += 1u;
            if (pmm_used > 0) {
                pmm_used -= 1u;
            }
        }
    }
    if (phys < pmm_cursor) {
        pmm_cursor = phys;
    }
    pmm_leave();
}

void pmm_foreach_free_run(int (*cb)(uint64_t phys, uint64_t pages, void *user),
                          void *user) {
    uint64_t index;
    uint64_t n;
    uint64_t base;
    uint64_t length;
    uint64_t type;
    uint64_t addr;
    uint64_t end;
    uint64_t run_start;
    uint64_t run;

    if (cb == 0) {
        return;
    }
    pmm_enter();
    n = bootinfo_memmap_count();
    for (index = 0; index < n; ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        addr = (base + PMM_PAGE - 1ull) & ~(PMM_PAGE - 1ull);
        end = (base + length) & ~(PMM_PAGE - 1ull);
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }
        run = 0;
        run_start = addr;
        while (addr < end) {
            uint64_t page;
            uint64_t byte;

            page = addr / PMM_PAGE;
            byte = page / 8ull;
            if ((page & 7ull) == 0ull && byte < PMM_BITMAP_BYTES &&
                pmm_bitmap[byte] == 0xFFu) {
                if (run != 0 && cb(run_start, run, user) == 0) {
                    pmm_leave();
                    return;
                }
                run = 0;
                addr += 8ull * PMM_PAGE;
                run_start = addr;
                continue;
            }
            if (!page_in_range(addr) || bitmap_is_used(addr)) {
                if (run != 0 && cb(run_start, run, user) == 0) {
                    pmm_leave();
                    return;
                }
                run = 0;
                run_start = addr + PMM_PAGE;
            } else {
                if (run == 0) {
                    run_start = addr;
                }
                run += 1u;
            }
            addr += PMM_PAGE;
        }
        if (run != 0 && cb(run_start, run, user) == 0) {
            pmm_leave();
            return;
        }
    }
    pmm_leave();
}

uint64_t pmm_claim_at(uint64_t phys, uint64_t pages) {
    uint64_t i;
    uint64_t p;
    uint64_t got;

    if (pages == 0 || phys == 0) {
        return 0;
    }
    pmm_enter();
    for (i = 0; i < pages; ++i) {
        p = phys + i * PMM_PAGE;
        if (!page_in_range(p) || bitmap_is_used(p)) {
            pmm_leave();
            return 0;
        }
    }
    got = claim_run(phys, pages);
    pmm_leave();
    return got;
}

uint64_t pmm_usable_pages(void) {
    return pmm_usable;
}

uint64_t pmm_used_pages(void) {
    uint64_t used;

    pmm_enter();
    used = pmm_used;
    pmm_leave();
    return used;
}

uint64_t pmm_free_pages(void) {
    uint64_t free_count;

    pmm_enter();
    free_count = pmm_free_count;
    pmm_leave();
    return free_count;
}

void pmm_selftest(void) {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    volatile uint64_t *va;
    volatile uint64_t *vb;
    volatile uint64_t *vc;

    a = pmm_alloc();
    b = pmm_alloc();
    c = pmm_alloc();
    if (a == 0 || b == 0 || c == 0) {
        panic("pmm_selftest: alloc inicial falhou");
    }
    if (a == b || b == c || a == c) {
        panic("pmm_selftest: paginas repetidas");
    }

    va = (volatile uint64_t *)bootinfo_phys_to_virt(a);
    vb = (volatile uint64_t *)bootinfo_phys_to_virt(b);
    vc = (volatile uint64_t *)bootinfo_phys_to_virt(c);
    *va = 0xa1a1a1a1a1a1a1a1ull;
    *vb = 0xb2b2b2b2b2b2b2b2ull;
    *vc = 0xc3c3c3c3c3c3c3c3ull;
    if (*va != 0xa1a1a1a1a1a1a1a1ull ||
        *vb != 0xb2b2b2b2b2b2b2b2ull ||
        *vc != 0xc3c3c3c3c3c3c3c3ull) {
        panic("pmm_selftest: assinatura HHDM falhou");
    }

    pmm_free(a);
    pmm_free(b);
    d = pmm_alloc();
    if (d != a && d != b) {
        panic("pmm_selftest: alloc nao reutilizou pagina libertada");
    }

    serial_puts("pmm selftest reuse phys=");
    serial_write_hex(d);
    serial_puts("\npmm usable=");
    serial_write_u64(pmm_usable_pages());
    serial_puts(" used=");
    serial_write_u64(pmm_used_pages());
    serial_puts(" free=");
    serial_write_u64(pmm_free_pages());
    serial_puts("\n");

    pmm_free(c);
    pmm_free(d);
}

