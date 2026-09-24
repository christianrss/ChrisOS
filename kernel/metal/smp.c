#define LIMINE_API_REVISION 3
#include <limine.h>
#include <stdint.h>

#include "apic.h"
#include "bootinfo.h"
#include "idt.h"
#include "job.h"
#include "mm.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"
#include "smp.h"
#include "sse_init.h"

volatile uint32_t cpu_online_count = 1u;
uint64_t kernel_cr3;

static uint64_t g_ap_stacks[SMP_MAX_APS];
static uint32_t g_ap_index_by_lapic[256];
static uint32_t g_cpu_by_lapic[256];
static uint32_t g_lapic_of_cpu[SMP_CPU_CAP];
static uint8_t g_lapic_known[SMP_CPU_CAP];
static int g_cpu_ready;

static void ap_entry(struct limine_mp_info *info);

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static struct limine_mp_info *ap_info_virt(struct limine_mp_info *info) {
    uintptr_t raw = (uintptr_t)info;

    if (info == 0) {
        return 0;
    }
    if ((raw >> 48) != 0xffffu) {
        return (struct limine_mp_info *)(uintptr_t)bootinfo_phys_to_virt(raw);
    }
    return info;
}

static uint32_t lapic_id_read(void) {
    volatile uint32_t *lapic = (volatile uint32_t *)mm_lapic_virt();
    if (!lapic) {
        return 0u;
    }
    return lapic[0x20u / 4u] >> 24;
}

static uint64_t alloc_ap_stack(uint32_t index) {
    uint64_t virt = AP_STACK_VIRT_BASE + (uint64_t)index * 0x10000ull;
    uint32_t page;

    for (page = 0; page < AP_STACK_PAGES; page++) {
        uint64_t phys = pmm_alloc();
        if (phys == 0u) {
            panic("AP stack PMM");
        }
        map_4k(virt + (uint64_t)page * 4096ull, phys, 0x3u);
    }
    return virt + (uint64_t)AP_STACK_PAGES * 4096ull;
}

static void ap_entry(struct limine_mp_info *info) {
    uint32_t index = 0u;
    uint32_t lapic_id = 0u;
    uint64_t stack_top;
    struct limine_mp_info *vinfo = ap_info_virt(info);

    write_cr3(kernel_cr3);
    if (vinfo != 0) {
        index = (uint32_t)vinfo->extra_argument;
        lapic_id = vinfo->lapic_id;
    }
    if (index == 0u || index >= SMP_MAX_APS) {
        lapic_id = lapic_id_read();
        index = g_ap_index_by_lapic[lapic_id & 0xffu];
    }
    if (index == 0u || index >= SMP_MAX_APS) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    stack_top = g_ap_stacks[index];
    if (stack_top == 0u) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    __asm__ volatile (
        "mov %0, %%rsp\n"
        "mov %0, %%rbp\n"
        :
        : "r"(stack_top)
        : "memory");
    idt_load();
    sse_bsp_init();
    if (lapic_id == 0u) {
        lapic_id = lapic_id_read();
    }
    if (index < SMP_CPU_CAP) {
        g_lapic_of_cpu[index] = lapic_id;
        g_lapic_known[index] = 1u;
    }
    __sync_fetch_and_add(&cpu_online_count, 1u);
    /* IF stays clear until the BSP finishes install. Enabling the LAPIC and
     * unmasking an AP during the ATA copy kept that copy from finishing. */
    job_worker_forever(index);
}

void smp_init(void) {
    struct limine_mp_response *mp;
    uint64_t i;
    uint32_t next = 1u;
    uint32_t spins = 0u;
    uint32_t want;

    kernel_cr3 = read_cr3();
    cpu_online_count = 1u;

    mp = bootinfo_mp_response();
    if (mp == 0) {
        serial_puts("smp: no MP response\n");
        return;
    }

    for (i = 0; i < 256u; i++) {
        g_ap_index_by_lapic[i] = 0u;
        g_cpu_by_lapic[i] = 0u;
    }
    g_cpu_by_lapic[mp->bsp_lapic_id & 0xffu] = 0u;
    g_lapic_of_cpu[0] = mp->bsp_lapic_id;
    g_lapic_known[0] = 1u;
    g_cpu_ready = 1;

    for (i = 0; i < mp->cpu_count; i++) {
        struct limine_mp_info *info = mp->cpus[i];
        if (info == 0) {
            continue;
        }
        if (info->lapic_id != mp->bsp_lapic_id) {
            info->goto_address = 0;
            info->extra_argument = 0;
        }
    }
    __asm__ volatile ("" ::: "memory");

    for (i = 0; i < mp->cpu_count; i++) {
        struct limine_mp_info *info = mp->cpus[i];
        if (info == 0) {
            continue;
        }
        if (info->lapic_id == mp->bsp_lapic_id) {
            continue;
        }
        if (next >= SMP_MAX_APS) {
            break;
        }
        g_ap_index_by_lapic[info->lapic_id & 0xffu] = next;
        g_cpu_by_lapic[info->lapic_id & 0xffu] = next;
        if (next < SMP_CPU_CAP) {
            g_lapic_of_cpu[next] = info->lapic_id;
            g_lapic_known[next] = 1u;
        }
        g_ap_stacks[next] = alloc_ap_stack(next);
        info->extra_argument = (uint64_t)next;
        __asm__ volatile ("" ::: "memory");
        info->goto_address = ap_entry;
        next += 1u;
    }

    want = next;
    while (cpu_online_count < want && spins < 100000000u) {
        spins += 1u;
        __asm__ volatile ("pause");
    }

    serial_puts("cpu_online_count=");
    serial_write_u64(cpu_online_count);
    serial_puts(" (BSP+AP)\n");
    apic_enable_local();
}

uint32_t smp_lapic_of(uint32_t cpu, int *known) {
    if (cpu >= SMP_CPU_CAP || !g_lapic_known[cpu]) {
        if (known) {
            *known = 0;
        }
        return 0u;
    }
    if (known) {
        *known = 1;
    }
    return g_lapic_of_cpu[cpu];
}

uint32_t smp_current_cpu(void) {
    uint32_t lapic;
    uint32_t index;

    if (!g_cpu_ready) {
        return 0u;
    }
    lapic = lapic_id_read();
    index = g_cpu_by_lapic[lapic & 0xffu];
    if (index >= SMP_CPU_CAP) {
        return 0u;
    }
    return index;
}

uint32_t smp_current_cpu(void) {
    uint32_t lapic;
    uint32_t index;

    if (!g_cpu_ready) {
        return 0u;
    }
    lapic = lapic_id_read();
    index = g_cpu_by_lapic[lapic & 0xffu];
    if (index >= SMP_CPU_CAP) {
        return 0u;
    }
    return index;
}

uint32_t smp_cpu_count(void) {
    const struct bootinfo *boot = bootinfo_get();
    if (boot->cpu_count == 0u) {
        return 1u;
    }
    return (uint32_t)boot->cpu_count;
}
