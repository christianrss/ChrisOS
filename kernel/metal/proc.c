#include "proc.h"

#include "bootinfo.h"
#include "mm.h"
#include "panic.h"
#include "pmm.h"
#include "serial.h"
#include "smp.h"
#include "sock.h"
#include "syscall.h"

#define PROC_PAGES 288

typedef struct ProcPage {
    uint64_t virt;
    uint64_t phys;
} ProcPage;

typedef struct Proc {
    int used;
    int alive;
    int state;
    uint64_t cr3;
    uint64_t vm_bytes;
    uint64_t heap_brk;
    int npages;
    int fb_pages;
    char name[24];
    ProcPage pages[PROC_PAGES];
} Proc;

static Proc g_proc[PROC_MAX];
static int g_current;
static volatile int g_slice;
static ProcFault g_fault;

static int page_owned(const Proc *p, uint64_t virt);

static void copy_name(char *dst, const char *src) {
    int i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < 23) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void proc_init(void) {
    int i;
    for (i = 0; i < PROC_MAX; ++i) {
        g_proc[i].used = 0;
        g_proc[i].alive = 0;
        g_proc[i].state = PROC_ST_FREE;
        g_proc[i].cr3 = 0;
        g_proc[i].vm_bytes = 0;
        g_proc[i].heap_brk = PROC_HEAP_VIRT;
        g_proc[i].npages = 0;
        g_proc[i].fb_pages = 0;
        g_proc[i].name[0] = 0;
    }
    g_proc[PROC_KERNEL].used = 1;
    g_proc[PROC_KERNEL].alive = 1;
    g_proc[PROC_KERNEL].state = PROC_ST_READY;
    g_proc[PROC_KERNEL].cr3 = mm_kernel_cr3();
    copy_name(g_proc[PROC_KERNEL].name, "kernel");
    g_current = PROC_KERNEL;
    g_slice = 0;
    g_fault.valid = 0;
}

int proc_create(const char *name) {
    int i;
    uint64_t cr3;
    for (i = 1; i < PROC_MAX; ++i) {
        if (g_proc[i].used) {
            continue;
        }
        cr3 = mm_clone_kernel_space();
        if (cr3 == 0) {
            return -1;
        }
        g_proc[i].used = 1;
        g_proc[i].alive = 1;
        g_proc[i].state = PROC_ST_READY;
        g_proc[i].cr3 = cr3;
        g_proc[i].vm_bytes = 0;
        g_proc[i].heap_brk = PROC_HEAP_VIRT;
        g_proc[i].npages = 0;
        g_proc[i].fb_pages = 0;
        copy_name(g_proc[i].name, name);
        if (proc_commit(i, PROC_STACK_VIRT) != 0) {
            proc_destroy(i);
            return -1;
        }
        return i;
    }
    return -1;
}

void proc_destroy(int pid) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    if (g_current == pid) {
        proc_switch(PROC_KERNEL);
    }
    proc_release_user(pid);
    if (g_proc[pid].cr3 != 0 && g_proc[pid].cr3 != mm_kernel_cr3()) {
        mm_free_user_space(g_proc[pid].cr3);
    }
    syscall_close_owner(pid);
    sock_close_proc(pid);
    g_proc[pid].used = 0;
    g_proc[pid].state = PROC_ST_FREE;
    g_proc[pid].alive = 0;
    g_proc[pid].cr3 = 0;
}

int proc_current(void) {
    return g_current;
}

void proc_switch(int pid) {
    uint64_t cr3;
    /* Invariant: user processes run only on the BSP. Scheduler state is
     * global and is not safe to mutate from an AP. */
    if (smp_current_cpu() != 0u) {
        panic("process switch off BSP");
    }
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    cr3 = g_proc[pid].cr3;
    g_current = pid;
    if (cr3 != 0) {
        mm_switch(cr3);
    }
}

uint64_t proc_cr3(int pid) {
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    return g_proc[pid].cr3;
}

int proc_map_user(int pid, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return -1;
    }
    if (virt >= 0x0000800000000000ull) {
        return -1;
    }
    return mm_map_cr3(g_proc[pid].cr3, virt, phys, flags | MM_USER);
}

int proc_map_owned(int pid, uint64_t virt, uint64_t phys, uint64_t flags) {
    Proc *p;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return -1;
    }
    p = &g_proc[pid];
    virt &= ~(PMM_PAGE - 1ull);
    if (page_owned(p, virt) || p->npages >= PROC_PAGES) {
        return -1;
    }
    if (proc_map_user(pid, virt, phys, flags) != 0) {
        return -1;
    }
    p->pages[p->npages].virt = virt;
    p->pages[p->npages].phys = phys;
    p->npages++;
    return 0;
}

void proc_on_tick(void) {
    g_slice = 1;
}

int proc_slice_due(void) {
    return g_slice;
}

void proc_slice_ack(void) {
    g_slice = 0;
}

void proc_record_fault(int pid, int tid, uint64_t cr2, uint64_t rip) {
    g_fault.valid = 1;
    g_fault.pid = pid;
    g_fault.tid = tid;
    g_fault.cr2 = cr2;
    g_fault.rip = rip;
    if (pid > 0 && pid < PROC_MAX && g_proc[pid].used) {
        g_proc[pid].alive = 0;
    }
    serial_puts("proc fault pid=");
    serial_write_hex((uint64_t)(uint32_t)pid);
    serial_puts(" rip=");
    serial_write_hex(rip);
    serial_puts(" cr2=");
    serial_write_hex(cr2);
    serial_puts("\n");
}

const ProcFault *proc_last_fault(void) {
    return &g_fault;
}

int proc_alive(int pid) {
    if (pid < 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    return g_proc[pid].alive;
}

int proc_runnable(int pid) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used || !g_proc[pid].alive) {
        return 0;
    }
    return g_proc[pid].state == PROC_ST_READY;
}

void proc_block(int pid, int why) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    g_proc[pid].state = why;
}

void proc_unblock(int pid) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return;
    }
    g_proc[pid].state = PROC_ST_READY;
}

void proc_unblock_why(int why) {
    int i;
    for (i = 1; i < PROC_MAX; ++i) {
        if (g_proc[i].used && g_proc[i].state == why) {
            g_proc[i].state = PROC_ST_READY;
        }
    }
}

uint64_t proc_page_phys(int pid, uint64_t virt) {
    int i;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    virt &= ~(PMM_PAGE - 1ull);
    for (i = 0; i < g_proc[pid].npages; ++i) {
        if (g_proc[pid].pages[i].virt == virt) {
            return g_proc[pid].pages[i].phys;
        }
    }
    return 0;
}

static int page_owned(const Proc *p, uint64_t virt) {
    int i;
    virt &= ~(PMM_PAGE - 1ull);
    for (i = 0; i < p->npages; ++i) {
        if (p->pages[i].virt == virt) {
            return 1;
        }
    }
    return 0;
}

int proc_commit(int pid, uint64_t virt) {
    Proc *p;
    uint64_t phys;
    uint8_t *dst;
    uint32_t i;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return -1;
    }
    p = &g_proc[pid];
    virt &= ~(PMM_PAGE - 1ull);
    if (page_owned(p, virt)) {
        return 0;
    }
    if (p->npages >= PROC_PAGES) {
        return -1;
    }
    phys = pmm_alloc();
    if (phys == 0) {
        return -1;
    }
    dst = (uint8_t *)(uintptr_t)bootinfo_phys_to_virt(phys);
    for (i = 0; i < PMM_PAGE; ++i) {
        dst[i] = 0;
    }
    if (proc_map_user(pid, virt, phys, MM_PRESENT | MM_WRITE) != 0) {
        pmm_free(phys);
        return -1;
    }
    p->pages[p->npages].virt = virt;
    p->pages[p->npages].phys = phys;
    p->npages++;
    if (pid == g_current) {
        mm_flush_tlb();
    }
    return 0;
}

uint8_t *proc_vm_ptr(int pid) {
    (void)pid;
    return (uint8_t *)(uintptr_t)PROC_VM_VIRT;
}

uint64_t proc_set_vm(int pid, uint64_t bytes) {
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    if (bytes > (uint64_t)PROC_PAGES * PMM_PAGE) {
        bytes = (uint64_t)PROC_PAGES * PMM_PAGE;
    }
    g_proc[pid].vm_bytes = bytes;
    /* First page is resident. The rest arrives on a fault. */
    if (proc_commit(pid, PROC_VM_VIRT) != 0) {
        return 0;
    }
    return bytes;
}

int proc_fault_demand(int pid, uint64_t cr2) {
    Proc *p;
    uint64_t page;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used || !g_proc[pid].alive) {
        return 0;
    }
    p = &g_proc[pid];
    page = cr2 & ~(PMM_PAGE - 1ull);
    if (page >= PROC_VM_VIRT && page < PROC_VM_VIRT + p->vm_bytes) {
        return proc_commit(pid, page) == 0;
    }
    if (page >= PROC_STACK_VIRT && page < PROC_STACK_VIRT + 4ull * PMM_PAGE) {
        return proc_commit(pid, page) == 0;
    }
    if (page >= PROC_HEAP_VIRT && page < p->heap_brk) {
        return proc_commit(pid, page) == 0;
    }
    if (p->fb_pages > 0 && page >= PROC_FB_VIRT &&
        page < PROC_FB_VIRT + (uint64_t)p->fb_pages * PMM_PAGE) {
        return proc_commit(pid, page) == 0;
    }
    return 0;
}

void proc_release_user(int pid) {
    Proc *p;
    int i;
    if (pid <= 0 || pid >= PROC_MAX) {
        return;
    }
    p = &g_proc[pid];
    for (i = 0; i < p->npages; ++i) {
        if (p->pages[i].phys) {
            if (p->cr3 != 0) {
                mm_unmap_cr3(p->cr3, p->pages[i].virt);
            }
            pmm_free(p->pages[i].phys);
            p->pages[i].phys = 0;
            p->pages[i].virt = 0;
        }
    }
    p->npages = 0;
    p->vm_bytes = 0;
    p->fb_pages = 0;
    p->heap_brk = PROC_HEAP_VIRT;
}

uint64_t proc_sbrk(int pid, uint64_t inc) {
    uint64_t old;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used) {
        return 0;
    }
    old = g_proc[pid].heap_brk;
    if (old + inc > PROC_HEAP_VIRT + (256ull * 1024ull)) {
        return 0;
    }
    g_proc[pid].heap_brk = old + inc;
    return old;
}

uint8_t *proc_fb_ptr(int pid, int pages) {
    int i;
    if (pid <= 0 || pid >= PROC_MAX || !g_proc[pid].used || pages < 1) {
        return 0;
    }
    if (pages > 16) {
        pages = 16;
    }
    g_proc[pid].fb_pages = pages;
    for (i = 0; i < pages; ++i) {
        if (proc_commit(pid, PROC_FB_VIRT + (uint64_t)i * PMM_PAGE) != 0) {
            return 0;
        }
    }
    return (uint8_t *)(uintptr_t)PROC_FB_VIRT;
}
