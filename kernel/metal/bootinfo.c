#define LIMINE_API_REVISION 3
#include <limine.h>
#include "bootinfo.h"
#include "panic.h"
#include "serial.h"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST,
    .revision = 0,
    .response = 0,
    .flags = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static struct bootinfo info;
static struct limine_memmap_response *memmap_response;
static int bootinfo_ready;

static const char *memmap_type_name(uint64_t type) {
    switch (type) {
    case LIMINE_MEMMAP_USABLE:
        return "USABLE";
    case LIMINE_MEMMAP_RESERVED:
        return "RESERVED";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return "ACPI_RECLAIM";
    case LIMINE_MEMMAP_ACPI_NVS:
        return "ACPI_NVS";
    case LIMINE_MEMMAP_BAD_MEMORY:
        return "BAD";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return "BOOT_RECLAIM";
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        return "EXECUTABLE";
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return "FRAMEBUFFER";
    default:
        return "UNKNOWN";
    }
}

static void dump_lapic_not_ram(void) {
    uint64_t index;
    uint64_t base;
    uint64_t length;
    uint64_t type;
    int found = 0;

    serial_puts("HHDM+LAPIC=");
    serial_write_hex(bootinfo_phys_to_virt(0xfee00000ull));
    serial_puts(" nao e mapeamento MMIO valido; nao desreferenciar\n");

    for (index = 0; index < bootinfo_memmap_count(); ++index) {
        if (bootinfo_memmap_entry(index, &base, &length, &type) != 0) {
            continue;
        }
        if (0xfee00000ull >= base && 0xfee00000ull < base + length) {
            found = 1;
            serial_puts("LAPIC 0xfee00000 cai no memmap type=");
            serial_write_u64(type);
            serial_puts(" ");
            serial_puts(memmap_type_name(type));
            serial_puts("\n");
            if (type == LIMINE_MEMMAP_USABLE) {
                panic("LAPIC marcado como USABLE");
            }
        }
    }
    if (!found) {
        serial_puts("LAPIC 0xfee00000 fora do memmap (MMIO, nao RAM)\n");
    }
}

void bootinfo_init(void) {
    struct limine_framebuffer *framebuffer;
    struct limine_hhdm_response *hhdm;
    struct limine_memmap_response *memmap;
    struct limine_mp_response *mp;
    struct limine_memmap_entry *entry;
    uint64_t index;
    uint64_t usable;

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        panic("Limine base revision 3 nao suportada");
    }
    if (framebuffer_request.response == 0 ||
        framebuffer_request.response->framebuffer_count == 0 ||
        framebuffer_request.response->framebuffers == 0) {
        panic("framebuffer Limine ausente");
    }
    if (hhdm_request.response == 0) {
        panic("HHDM Limine ausente");
    }
    if (memmap_request.response == 0 ||
        memmap_request.response->entries == 0) {
        panic("memmap Limine ausente");
    }
    if (mp_request.response == 0) {
        panic("MP Limine ausente");
    }

    framebuffer = framebuffer_request.response->framebuffers[0];
    if (framebuffer == 0 || framebuffer->bpp != 32) {
        panic("framebuffer nao e 32 bpp");
    }

    hhdm = hhdm_request.response;
    memmap = memmap_request.response;
    mp = mp_request.response;
    if (mp->cpu_count == 0 || mp->cpus == 0) {
        panic("MP sem CPUs");
    }

    usable = 0;
    for (index = 0; index < memmap->entry_count; ++index) {
        entry = memmap->entries[index];
        if (entry == 0) {
            panic("entrada de memmap nula");
        }
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable += entry->length;
        }
    }

    info.hhdm_offset = hhdm->offset;
    info.fb_addr = (uint64_t)framebuffer->address;
    info.fb_width = framebuffer->width;
    info.fb_height = framebuffer->height;
    info.fb_pitch = framebuffer->pitch;
    info.fb_bpp = framebuffer->bpp;
    info.usable_bytes = usable;
    info.memmap_entries = memmap->entry_count;
    info.cpu_count = mp->cpu_count;
    info.bsp_lapic_id = mp->bsp_lapic_id;
    memmap_response = memmap;
    bootinfo_ready = 1;

    serial_puts("ChrisOS: bootinfo revision 3\n");
    serial_puts("HHDM offset=");
    serial_write_hex(info.hhdm_offset);
    serial_puts("\nfb ");
    serial_write_u64(info.fb_width);
    serial_puts("x");
    serial_write_u64(info.fb_height);
    serial_puts(" pitch=");
    serial_write_u64(info.fb_pitch);
    serial_puts(" bpp=");
    serial_write_u64(info.fb_bpp);
    serial_puts(" addr=");
    serial_write_hex(info.fb_addr);
    serial_puts("\n");

    for (index = 0; index < memmap->entry_count; ++index) {
        entry = memmap->entries[index];
        serial_puts("mmap[");
        serial_write_u64(index);
        serial_puts("] type=");
        serial_write_u64(entry->type);
        serial_puts(" ");
        serial_puts(memmap_type_name(entry->type));
        serial_puts(" base=");
        serial_write_hex(entry->base);
        serial_puts(" len=");
        serial_write_hex(entry->length);
        serial_puts("\n");
    }

    serial_puts("usable_bytes=");
    serial_write_u64(info.usable_bytes);
    serial_puts(" memmap_entries=");
    serial_write_u64(info.memmap_entries);
    serial_puts("\ncpu_count=");
    serial_write_u64(info.cpu_count);
    serial_puts("\nbsp_lapic_id=");
    serial_write_u64(info.bsp_lapic_id);
    serial_puts("\n");

    dump_lapic_not_ram();
}

struct limine_mp_response *bootinfo_mp_response(void) {
    if (!bootinfo_ready || mp_request.response == 0) {
        return 0;
    }
    return mp_request.response;
}

const struct bootinfo *bootinfo_get(void) {
    if (!bootinfo_ready) {
        panic("bootinfo_get antes de bootinfo_init");
    }
    return &info;
}

uint64_t bootinfo_phys_to_virt(uint64_t phys) {
    if (!bootinfo_ready) {
        panic("bootinfo_phys_to_virt antes de bootinfo_init");
    }
    return phys + info.hhdm_offset;
}

uint64_t bootinfo_memmap_count(void) {
    if (!bootinfo_ready || memmap_response == 0) {
        panic("bootinfo_memmap_count antes de bootinfo_init");
    }
    return memmap_response->entry_count;
}

int bootinfo_memmap_entry(uint64_t index, uint64_t *base, uint64_t *length, uint64_t *type) {
    struct limine_memmap_entry *entry;

    if (!bootinfo_ready || memmap_response == 0) {
        panic("bootinfo_memmap_entry antes de bootinfo_init");
    }
    if (base == 0 || length == 0 || type == 0) {
        return -1;
    }
    if (index >= memmap_response->entry_count) {
        return -1;
    }
    entry = memmap_response->entries[index];
    if (entry == 0) {
        return -1;
    }
    *base = entry->base;
    *length = entry->length;
    *type = entry->type;
    return 0;
}
