#include "jit.h"

#include "clvm/clvm_vm.h"
#include "clvm_sys.h"
#include "mm.h"
#include "pmm.h"

#define JIT_VIRT_BASE 0xffffffff92000000ull
#define JIT_VIRT_LIMIT (JIT_VIRT_BASE + 16ull * PMM_PAGE)

static uint64_t g_jit_virt_next = JIT_VIRT_BASE;
static ClvmVm *g_jit_vm;
static void *g_jit_user;

int jit_alloc(JitBuf *buf) {
    if (buf == NULL) {
        return -1;
    }
    buf->phys = pmm_alloc();
    if (buf->phys == 0) {
        return -1;
    }
    if (g_jit_virt_next + PMM_PAGE > JIT_VIRT_LIMIT) {
        pmm_free(buf->phys);
        buf->phys = 0;
        return -1;
    }
    buf->w = (uint8_t *)(uintptr_t)g_jit_virt_next;
    buf->x = buf->w;
    map_4k(g_jit_virt_next, buf->phys, MM_PRESENT | MM_WRITE | MM_NX);
    g_jit_virt_next += PMM_PAGE;
    buf->used = 0;
    buf->cap = JIT_MAX;
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
    if (buf == NULL || buf->x == NULL || buf->phys == 0) {
        return;
    }
    map_4k((uint64_t)(uintptr_t)buf->x, buf->phys, MM_PRESENT);
}

void jit_free(JitBuf *buf) {
    if (buf == NULL || buf->phys == 0) {
        return;
    }
    pmm_free(buf->phys);
    buf->phys = 0;
    buf->w = NULL;
    buf->x = NULL;
    buf->used = 0;
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
