#include "jit.h"

#include "bootinfo.h"
#include "clvm/clvm_vm.h"
#include "clvm_sys.h"
#include "mm.h"
#include "pmm.h"
#include "serial.h"
#include "smp.h"

/*
 * Kernel/Limine live in the 1GiB at 0xffffffff80000000 (PDPT[2]), together
 * with LAPIC (90000000) and the old JIT window (92000000). Mapping 4K pages
 * there and reloading CR3 left the machine dead mid-serial. PDPT[3] is empty.
 */
#define JIT_VIRT_BASE 0xffffffffc0000000ull
#define JIT_VIRT_LIMIT (JIT_VIRT_BASE + 8192ull * PMM_PAGE)

static uint64_t g_jit_virt_next = JIT_VIRT_BASE;

#define JIT_VA_SLOTS 128u

typedef struct JitVa {
    uint64_t virt;
    uint32_t pages;
    int used;
} JitVa;

static JitVa g_jit_va[JIT_VA_SLOTS];

typedef struct JitSysCtx {
    ClvmVm *vm;
    void *user;
} JitSysCtx;

/* Per CPU. One global would let VM A on CPU 0 replace the trampoline
 * context VM B is using on CPU 1. */
static JitSysCtx g_jit_ctx[SMP_CPU_CAP];

static uint64_t jit_va_alloc(uint32_t pages) {
    uint32_t i;
    uint64_t virt;

    for (i = 0; i < JIT_VA_SLOTS; ++i) {
        if (g_jit_va[i].virt != 0 && !g_jit_va[i].used &&
            g_jit_va[i].pages == pages) {
            g_jit_va[i].used = 1;
            return g_jit_va[i].virt;
        }
    }
    if (g_jit_virt_next + (uint64_t)pages * PMM_PAGE > JIT_VIRT_LIMIT) {
        return 0;
    }
    virt = g_jit_virt_next;
    g_jit_virt_next += (uint64_t)pages * PMM_PAGE;
    for (i = 0; i < JIT_VA_SLOTS; ++i) {
        if (g_jit_va[i].virt == 0) {
            g_jit_va[i].virt = virt;
            g_jit_va[i].pages = pages;
            g_jit_va[i].used = 1;
            break;
        }
    }
    return virt;
}

static void jit_va_free(uint64_t virt) {
    uint32_t i;
    for (i = 0; i < JIT_VA_SLOTS; ++i) {
        if (g_jit_va[i].virt == virt) {
            g_jit_va[i].used = 0;
            return;
        }
    }
}

int jit_alloc(JitBuf *buf) {
    uint64_t virt;

    if (buf == NULL) {
        return -1;
    }
    if (buf->pages == 0 || buf->pages > JIT_PAGES) {
        buf->pages = JIT_PAGES;
    }
    serial_puts("jit: alloc begin pages=");
    serial_write_u64((uint64_t)buf->pages);
    serial_puts("\n");
    buf->phys = pmm_alloc_contig(buf->pages);
    if (buf->phys == 0) {
        serial_puts("jit: alloc contig failed\n");
        return -1;
    }
    serial_puts("jit: alloc phys=");
    serial_write_hex(buf->phys);
    serial_puts("\n");
    virt = jit_va_alloc(buf->pages);
    if (virt == 0) {
        pmm_free_contig(buf->phys, buf->pages);
        buf->phys = 0;
        serial_puts("jit: alloc virt exhausted\n");
        return -1;
    }
    buf->w = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(buf->phys);
    buf->x = (uint8_t *)(uintptr_t)virt;
    buf->used = 0;
    buf->cap = (uint32_t)buf->pages * (uint32_t)PMM_PAGE;
    serial_puts("jit: alloc hhdm cap=");
    serial_write_u64((uint64_t)buf->cap);
    serial_puts("\n");
    return 0;
}

int jit_emit(JitBuf *buf, const uint8_t *bytes, uint32_t n) {
    uint32_t i;

    if (buf == NULL || bytes == NULL || buf->used + n > buf->cap) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        buf->w[buf->used++] = bytes[i];
    }
    return 0;
}

void jit_seal(JitBuf *buf) {
    uint32_t i;
    uint64_t virt;

    if (buf == NULL || buf->x == NULL || buf->phys == 0) {
        return;
    }
    serial_puts("jit: map exec pages=");
    serial_write_u64((uint64_t)buf->pages);
    serial_puts("\n");
    virt = (uint64_t)(uintptr_t)buf->x;
    /*
     * These translations were never present, so a global CR3 reload is
     * unnecessary. Reloading CR3 here (via the TLB IPI) killed the machine
     * mid-serial, before "jit: map exec done" finished. invlpg drops a
     * cached not-present entry on this CPU only. Another CPU picks the
     * mapping up when it loads CR3 to run the process.
     */
    for (i = 0; i < buf->pages; ++i) {
        map_4k(virt + (uint64_t)i * PMM_PAGE,
               buf->phys + (uint64_t)i * PMM_PAGE,
               MM_PRESENT);
    }
    serial_puts("jit: map exec done\n");
}

void jit_free(JitBuf *buf) {
    uint32_t i;
    uint32_t pages;
    uint64_t virt;

    if (buf == NULL || buf->phys == 0) {
        return;
    }
    pages = buf->pages ? buf->pages : 1u;
    serial_puts("close: jit_free pages=");
    serial_write_u64(pages);
    serial_puts("\n");
    if (buf->x != NULL) {
        int synced;
        virt = (uint64_t)(uintptr_t)buf->x;
        serial_puts("close: unmap begin\n");
        for (i = 0; i < pages; ++i) {
            unmap_4k(virt + (uint64_t)i * PMM_PAGE);
        }
        serial_puts("close: unmap done\n");
        synced = mm_tlb_shootdown_range(virt, (uint64_t)pages * PMM_PAGE) == 0;
        serial_puts("close: shootdown rc=");
        serial_write_u64(synced ? 0u : 1u);
        serial_puts("\n");
        jit_va_free(virt);
        if (synced) {
            serial_puts("close: pmm free\n");
            pmm_free_contig(buf->phys, pages);
        } else {
            serial_puts("close: quarantine\n");
            mm_tlb_quarantine(buf->phys, pages);
        }
    } else {
        serial_puts("close: pmm free no virt\n");
        pmm_free_contig(buf->phys, pages);
    }
    serial_puts("close: jit_free done\n");
    buf->phys = 0;
    buf->w = NULL;
    buf->x = NULL;
    buf->used = 0;
    buf->pages = 0;
}

void jit_set_sys_context(ClvmVm *vm, void *user) {
    uint32_t cpu = smp_current_cpu();
    if (cpu >= SMP_CPU_CAP) {
        cpu = 0u;
    }
    g_jit_ctx[cpu].vm = vm;
    g_jit_ctx[cpu].user = user;
}

int jit_sys_trampoline(int32_t id) {
    uint32_t cpu = smp_current_cpu();
    JitSysCtx *ctx;
    if (cpu >= SMP_CPU_CAP) {
        cpu = 0u;
    }
    ctx = &g_jit_ctx[cpu];
    if (ctx->vm == NULL) {
        return -1;
    }
    return clvm_sys_dispatch(ctx->vm, id, ctx->user);
}
