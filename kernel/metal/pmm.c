#define LIMINE_API_REVISION 3
#include <limine.h>
#include "pmm.h"
#include "bootinfo.h"
#include "panic.h"
#include "serial.h"

#define PMM_PAGE_COUNT (PMM_MAX_PHYS / PMM_PAGE)
#define PMM_BITMAP_BYTES (PMM_PAGE_COUNT / 8ull)

extern char __kernel_start[];
extern char __kernel_end[];

static uint8_t pmm_bitmap[PMM_BITMAP_BYTES];
static uint64_t pmm_usable;
static uint64_t pmm_used;
static uint64_t pmm_free_count;

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
}

uint64_t pmm_alloc(void) {
    return pmm_alloc_contig(1u);
}

uint64_t pmm_alloc_contig(uint32_t count) {
    uint64_t page;
    uint64_t start;
    uint64_t phys;
    uint32_t run;

    if (count == 0u) {
        return 0;
    }
    for (page = 0; page + count <= PMM_PAGE_COUNT; ++page) {
        start = page * PMM_PAGE;
        for (run = 0; run < count; ++run) {
            phys = start + (uint64_t)run * PMM_PAGE;
            if (bitmap_is_used(phys)) {
                break;
            }
        }
        if (run != count) {
            continue;
        }
        for (run = 0; run < count; ++run) {
            phys = start + (uint64_t)run * PMM_PAGE;
            bitmap_set_used(phys);
            if (pmm_free_count > 0) {
                pmm_free_count -= 1u;
            }
            pmm_used += 1u;
        }
        return start;
    }
    return 0;
}

void pmm_free(uint64_t phys) {
    pmm_free_contig(phys, 1u);
}

void pmm_free_contig(uint64_t phys, uint32_t count) {
    uint32_t run;

    if (phys == 0 || count == 0u) {
        return;
    }
    for (run = 0; run < count; ++run) {
        uint64_t page_phys = phys + (uint64_t)run * PMM_PAGE;
        if (!page_in_range(page_phys)) {
            panic("pmm_free_contig desalinhado ou fora de 256MiB");
        }
        if (bitmap_is_used(page_phys)) {
            bitmap_set_free(page_phys);
            pmm_free_count += 1u;
            if (pmm_used > 0) {
                pmm_used -= 1u;
            }
        }
    }
}

uint64_t pmm_usable_pages(void) {
    return pmm_usable;
}

uint64_t pmm_used_pages(void) {
    return pmm_used;
}

uint64_t pmm_free_pages(void) {
    return pmm_free_count;
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

