#include "jit.h"

#include "clvm/clvm_vm.h"
#include "clvm_sys.h"
#include "mm.h"
#include "pmm.h"
#include "serial.h"

#define JIT_VIRT_BASE 0xffffffff92000000ull
#define JIT_VIRT_LIMIT (JIT_VIRT_BASE + 8192ull * PMM_PAGE)

static uint64_t g_jit_virt_next = JIT_VIRT_BASE;
static ClvmVm *g_jit_vm;
static void *g_jit_user;

int jit_alloc(JitBuf *buf) {
    uint32_t i;
    uint64_t virt;

    if (buf == NULL) {
        return -1;
    }
    serial_puts("jit: alloc begin pages=");
    serial_write_u64((uint64_t)JIT_PAGES);
    serial_puts("\n");
    buf->pages = JIT_PAGES;
    buf->phys = pmm_alloc_contig(buf->pages);
    if (buf->phys == 0) {
        serial_puts("jit: alloc contig failed\n");
        return -1;
    }
    serial_puts("jit: alloc phys=");
    serial_write_hex(buf->phys);
    serial_puts("\n");
    if (g_jit_virt_next + (uint64_t)buf->pages * PMM_PAGE > JIT_VIRT_LIMIT) {
        pmm_free_contig(buf->phys, buf->pages);
        buf->phys = 0;
        serial_puts("jit: alloc virt exhausted\n");
        return -1;
    }
    virt = g_jit_virt_next;
    buf->w = (uint8_t *)(uintptr_t)virt;
    buf->x = buf->w;
    for (i = 0; i < buf->pages; ++i) {
        map_4k(virt + (uint64_t)i * PMM_PAGE,
               buf->phys + (uint64_t)i * PMM_PAGE,
               MM_PRESENT | MM_WRITE | MM_NX);
    }
    g_jit_virt_next += (uint64_t)buf->pages * PMM_PAGE;
    buf->used = 0;
    buf->cap = (uint32_t)buf->pages * (uint32_t)PMM_PAGE;
    serial_puts("jit: alloc map done cap=");
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
    virt = (uint64_t)(uintptr_t)buf->x;
    for (i = 0; i < buf->pages; ++i) {
        map_4k(virt + (uint64_t)i * PMM_PAGE,
               buf->phys + (uint64_t)i * PMM_PAGE,
               MM_PRESENT);
    }
}

void jit_free(JitBuf *buf) {
    if (buf == NULL || buf->phys == 0) {
        return;
    }
    pmm_free_contig(buf->phys, buf->pages ? buf->pages : 1u);
    buf->phys = 0;
    buf->w = NULL;
    buf->x = NULL;
    buf->used = 0;
    buf->pages = 0;
}

void jit_set_sys_context(ClvmVm *vm, void *user) {
    g_jit_vm = vm;
    g_jit_user = user;
}

int jit_sys_trampoline(int32_t id) {
    if (g_jit_vm == NULL) {
        return -1;
    }
    return clvm_sys_dispatch(g_jit_vm, id, g_jit_user);
}
